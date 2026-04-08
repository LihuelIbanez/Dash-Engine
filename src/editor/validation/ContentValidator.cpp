#include "ContentValidator.h"
#include "SceneData.h"
#include "World.h"
#include "AssetDatabase.h"
#include "Components.h"
#include "GridNav.h"
#include "IsoRenderer.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using Severity = ValidationIssue::Severity;

// ─────────────────────────────────────────────────────────────────────────────
static ValidationIssue err(const std::string& msg, uint64_t eid = 0,
                            int tx = -1, int ty = -1)
{
    return {Severity::Error, msg, eid, tx, ty};
}
static ValidationIssue warn(const std::string& msg, uint64_t eid = 0,
                             int tx = -1, int ty = -1)
{
    return {Severity::Warning, msg, eid, tx, ty};
}

// ─────────────────────────────────────────────────────────────────────────────
std::vector<ValidationIssue>
ContentValidator::validate(const SceneData& scene,
                           const World& world,
                           const AssetDatabase& db) const
{
    std::vector<ValidationIssue> issues;

    // ── Gameplay: player presence ─────────────────────────────────────────────
    int playerCount = 0;
    EntityData const* playerEntity = nullptr;
    for (const auto& e : scene.entities) {
        if (e.type == EntityData::Type::Player) {
            ++playerCount;
            playerEntity = &e;
        }
    }
    if (playerCount == 0)
        issues.push_back(err("Scene has no Player entity."));
    else if (playerCount > 1)
        issues.push_back(err("Scene has more than one Player entity (" +
                             std::to_string(playerCount) + " found)."));

    // ── Data: duplicate IDs ───────────────────────────────────────────────────
    std::unordered_map<uint64_t, int> idCount;
    for (const auto& e : scene.entities)
        ++idCount[e.id];
    for (auto& [id, cnt] : idCount)
        if (cnt > 1)
            issues.push_back(err("Duplicate entity ID: " + std::to_string(id), id));

    for (const auto& e : scene.entities) {
        const std::string prefix = "Entity '" + e.name + "' (" + std::to_string(e.id) + "): ";

        // ── Map: out-of-bounds position ───────────────────────────────────────
        if (e.x < 0.f || e.x >= static_cast<float>(WORLD_W) ||
            e.y < 0.f || e.y >= static_cast<float>(WORLD_H)) {
            issues.push_back(err(prefix + "position out of world bounds (" +
                                 std::to_string(static_cast<int>(e.x)) + "," +
                                 std::to_string(static_cast<int>(e.y)) + ").",
                                 e.id));
        }

        // ── Map: player on non-walkable tile ──────────────────────────────────
        if (e.type == EntityData::Type::Player && !world.isWalkable(e.x, e.y))
            issues.push_back(err(prefix + "Player is on a non-walkable tile.", e.id));

        // ── Data: missing TransformComponent ─────────────────────────────────
        bool hasTransform = false;
        for (const auto& comp : e.components)
            if (std::holds_alternative<TransformComponent>(comp)) { hasTransform = true; break; }
        if (!hasTransform)
            issues.push_back(warn(prefix + "no TransformComponent.", e.id));

        // ── Data: HealthComponent validation ─────────────────────────────────
        for (const auto& comp : e.components) {
            if (std::holds_alternative<HealthComponent>(comp)) {
                const auto& hp = std::get<HealthComponent>(comp);
                if (hp.maxHealth <= 0)
                    issues.push_back(warn(prefix + "HealthComponent.maxHealth <= 0.", e.id));
                else if (hp.health > hp.maxHealth)
                    issues.push_back(warn(prefix + "HealthComponent.health > maxHealth.", e.id));
            }
        }

        // ── Data: StatsComponent with negative values ─────────────────────────
        for (const auto& comp : e.components) {
            if (std::holds_alternative<StatsComponent>(comp)) {
                const auto& st = std::get<StatsComponent>(comp);
                if (st.attack < 0 || st.defense < 0 || st.magicAttack < 0 || st.speed < 0)
                    issues.push_back(warn(prefix + "StatsComponent has negative values.", e.id));
            }
        }

        // ── Data: broken prefab GUID ──────────────────────────────────────────
        if (!e.prefabGuid.empty() && db.findByGuid(e.prefabGuid) == nullptr)
            issues.push_back(warn(prefix + "prefabGuid '" + e.prefabGuid +
                                  "' not found in AssetDatabase.", e.id));

        // ── Gameplay: enemy unreachable from Player ───────────────────────────
        if (e.type == EntityData::Type::Enemy && playerEntity != nullptr) {
            // Only test if enemy is in-bounds; out-of-bounds entities already flagged
            if (e.x >= 0.f && e.x < static_cast<float>(WORLD_W) &&
                e.y >= 0.f && e.y < static_cast<float>(WORLD_H)) {
                NavPoint ep = GridNav::worldToTile(e.x, e.y);
                NavPoint pp = GridNav::worldToTile(playerEntity->x, playerEntity->y);
                // Limit A* search depth for editor validation
                auto path = GridNav::findPath(ep.x, ep.y, pp.x, pp.y, world, 1024);
                if (path.empty())
                    issues.push_back(warn(prefix +
                        "Enemy has no walkable A* path to the Player.", e.id));
            }
        }
    }

    // ── Map: tiles with unknown/invalid type ──────────────────────────────────
    // TileType is an enum class from 0..8; values outside that range are invalid.
    for (int ty = 0; ty < WORLD_H; ++ty) {
        for (int tx = 0; tx < WORLD_W; ++tx) {
            int tv = static_cast<int>(world.grid[ty][tx].type);
            if (tv < 0 || tv > static_cast<int>(TileType::Snow)) {
                issues.push_back(warn("Tile at (" + std::to_string(tx) + "," +
                                      std::to_string(ty) + ") has unknown type " +
                                      std::to_string(tv) + ".",
                                      0, tx, ty));
            }
        }
    }

    return issues;
}
