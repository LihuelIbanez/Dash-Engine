#include "rendering/vulkan/SceneLoader.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <variant>

#include <nlohmann/json.hpp>

namespace dash::vkexp {

using json = nlohmann::json;

namespace {

// Ground offset applied so entities sit on top of the terrain instead of
// intersecting it. TransformComponent::z is added on top of this.
constexpr float kPlayerBaseHeight = 1.0f;
constexpr float kEnemyBaseHeight  = 0.6f;

} // namespace

LoadedScene SceneLoader::load(const std::string& scenePath)
{
    LoadedScene out;

    std::ifstream in(scenePath);
    if (!in.is_open()) return out;

    json j;
    try {
        in >> j;
    } catch (...) {
        return out;
    }

    if (!out.data.loadFromJson(j)) return out;

    // Tilemap is appended by the editor on export; it is not part of SceneData.
    if (j.contains("tilemap") && j["tilemap"].is_array()) {
        const auto& tilemap = j["tilemap"];
        out.tilemap.reserve(tilemap.size());
        for (const auto& t : tilemap)
            out.tilemap.push_back(t.is_number() ? t.get<int>() : 3);

        out.worldWidth = j.value("worldWidth", 64);
        out.worldHeight = j.value("worldHeight",
            out.worldWidth > 0 ? static_cast<int>(out.tilemap.size()) / out.worldWidth : 0);
    }

    out.valid = true;
    return out;
}

bool SceneLoader::loadSpawnPoint(const LoadedScene& scene, dash::physics::Vec3& outSpawn)
{
    if (!scene.valid || scene.data.entities.empty()) return false;

    const EntityData* selected = nullptr;
    for (const auto& e : scene.data.entities) {
        if (e.type == EntityData::Type::Player) { selected = &e; break; }
    }
    if (!selected) selected = &scene.data.entities.front();

    // Dash scenes store horizontal position in (x, y). Vulkan baseline uses y-up,
    // so map scene y to z and keep a default spawn height on physics y.
    outSpawn = {selected->x, 0.8f, selected->y};
    return true;
}

std::vector<RenderInstance> SceneLoader::loadInstances(const LoadedScene& scene)
{
    std::vector<RenderInstance> out;
    if (!scene.valid) return out;

    out.reserve(scene.data.entities.size());
    for (const auto& e : scene.data.entities) {
        const bool isPlayer = (e.type == EntityData::Type::Player);

        RenderInstance inst;
        inst.isPlayer = isPlayer;
        inst.entityId = e.id;
        inst.color = isPlayer ? dash::physics::Vec3{0.30f, 0.58f, 0.95f}
                              : dash::physics::Vec3{0.82f, 0.34f, 0.34f};
        const dash::physics::Vec3 baseScale = isPlayer
            ? dash::physics::Vec3{0.26f, 0.52f, 0.26f}
            : dash::physics::Vec3{0.22f, 0.40f, 0.22f};
        const float baseHeight = isPlayer ? kPlayerBaseHeight : kEnemyBaseHeight;

        inst.position = {e.x, baseHeight, e.y};
        inst.scale = baseScale;

        for (const auto& c : e.components) {
            if (const auto* tf = std::get_if<TransformComponent>(&c)) {
                inst.position = {tf->x, baseHeight + tf->z, tf->y};
                inst.yawDeg = tf->yawDeg;
                inst.pitchDeg = tf->pitchDeg;
                inst.rollDeg = tf->rollDeg;
                const float s = tf->scale > 0.0f ? tf->scale : 1.0f;
                inst.scale = {baseScale.x * s, baseScale.y * s, baseScale.z * s};
            } else if (const auto* rc = std::get_if<RenderComponent>(&c)) {
                inst.visible = rc->visible;
                inst.layer = rc->layer;
                inst.renderMode = rc->renderMode;
                if (!rc->mesh.empty()) inst.meshId = rc->mesh;
                if (!rc->material.empty()) inst.materialId = rc->material;
            }
        }

        out.push_back(std::move(inst));
    }
    return out;
}

bool SceneLoader::loadPlayerPosition(const LoadedScene& scene, float& outX, float& outZ)
{
    if (!scene.valid) return false;

    for (const auto& e : scene.data.entities) {
        if (e.type != EntityData::Type::Player) continue;
        outX = e.x;
        outZ = e.y;
        for (const auto& c : e.components) {
            if (const auto* tf = std::get_if<TransformComponent>(&c)) {
                outX = tf->x;
                outZ = tf->y;
                break;
            }
        }
        return true;
    }
    return false;
}

std::vector<PhysicsSpawn> SceneLoader::loadPhysicsBodies(const LoadedScene& scene)
{
    std::vector<PhysicsSpawn> out;
    if (!scene.valid) return out;

    for (const auto& e : scene.data.entities) {
        const PhysicsComponent* phys = nullptr;
        const TransformComponent* tf = nullptr;
        for (const auto& c : e.components) {
            if (const auto* p = std::get_if<PhysicsComponent>(&c)) phys = p;
            else if (const auto* t = std::get_if<TransformComponent>(&c)) tf = t;
        }
        if (!phys) continue;

        const bool isPlayer = (e.type == EntityData::Type::Player);
        const float baseHeight = isPlayer ? kPlayerBaseHeight : kEnemyBaseHeight;

        PhysicsSpawn spawn;
        spawn.entityId = e.id;
        spawn.position = tf ? dash::physics::Vec3{tf->x, baseHeight + tf->z, tf->y}
                            : dash::physics::Vec3{e.x, baseHeight, e.y};
        spawn.halfExtents = {std::max(0.01f, phys->halfExtentX),
                             std::max(0.01f, phys->halfExtentY),
                             std::max(0.01f, phys->halfExtentZ)};
        spawn.mass = std::max(0.0001f, phys->mass);
        spawn.isStatic = phys->isStatic;
        out.push_back(spawn);
    }
    return out;
}

dash::physics::Vec3 SceneLoader::getTileColor(int tileType)
{
    switch (tileType) {
        case 0:  return {0.04f, 0.07f, 0.22f};   // DeepWater
        case 1:  return {0.08f, 0.14f, 0.31f};   // Water
        case 2:  return {0.43f, 0.35f, 0.20f};   // Sand
        case 3:  return {0.14f, 0.22f, 0.10f};   // Grass
        case 4:  return {0.08f, 0.16f, 0.06f};   // Forest
        case 5:  return {0.25f, 0.16f, 0.10f};   // Dirt
        case 6:  return {0.27f, 0.25f, 0.24f};   // Stone
        case 7:  return {0.22f, 0.20f, 0.19f};   // Mountain
        case 8:  return {0.63f, 0.65f, 0.69f};   // Snow
        default: return {0.14f, 0.22f, 0.10f};   // Default to Grass
    }
}

float SceneLoader::getTileHeight(int tileType)
{
    switch (tileType) {
        case 0: return -0.30f; // DeepWater
        case 1: return -0.16f; // Water
        case 2: return  0.00f; // Sand
        case 3: return  0.05f; // Grass
        case 4: return  0.14f; // Forest
        case 5: return  0.08f; // Dirt
        case 6: return  0.22f; // Stone
        case 7: return  0.42f; // Mountain
        case 8: return  0.50f; // Snow
        default: return 0.05f;
    }
}

float SceneLoader::sampleTerrainHeight(const std::vector<float>& mapHeights, int mapWidth, int mapHeight, float x, float z)
{
    if (mapHeights.empty() || mapWidth <= 0 || mapHeight <= 0) return 0.0f;
    int tx = static_cast<int>(std::round(x));
    int tz = static_cast<int>(std::round(z));
    tx = std::max(0, std::min(mapWidth - 1, tx));
    tz = std::max(0, std::min(mapHeight - 1, tz));
    return mapHeights[static_cast<size_t>(tz * mapWidth + tx)];
}

std::vector<RenderInstance> SceneLoader::loadTerrainInstances(
    const LoadedScene& scene,
    std::vector<float>* outHeightMap,
    int* outMapWidth,
    int* outMapHeight)
{
    std::vector<RenderInstance> out;
    if (outHeightMap) outHeightMap->clear();
    if (outMapWidth) *outMapWidth = 0;
    if (outMapHeight) *outMapHeight = 0;

    if (!scene.valid) return out;

    if (!scene.tilemap.empty() && scene.worldWidth > 0) {
        const int worldWidth = scene.worldWidth;
        const int worldHeight = scene.worldHeight;

        if (outHeightMap && worldWidth > 0 && worldHeight > 0) {
            outHeightMap->assign(static_cast<size_t>(worldWidth * worldHeight), 0.0f);
            if (outMapWidth) *outMapWidth = worldWidth;
            if (outMapHeight) *outMapHeight = worldHeight;
        }

        for (int idx = 0; idx < static_cast<int>(scene.tilemap.size()); ++idx) {
            const int y = idx / worldWidth;
            const int x = idx % worldWidth;

            const int tileType = scene.tilemap[static_cast<size_t>(idx)];
            const dash::physics::Vec3 color = getTileColor(tileType);
            const float h = getTileHeight(tileType);

            if (outHeightMap && idx < static_cast<int>(outHeightMap->size())) {
                (*outHeightMap)[static_cast<size_t>(idx)] = h;
            }

            RenderInstance tile;
            tile.position = {static_cast<float>(x), h, static_cast<float>(y)};
            tile.scale = {0.48f, 0.03f, 0.48f};
            tile.color = color;
            out.push_back(std::move(tile));
        }
        return out;
    }

    // Fallback: checkerboard around the entities when the scene has no tilemap
    int minX = 28;
    int maxX = 36;
    int minZ = 28;
    int maxZ = 36;

    if (!scene.data.entities.empty()) {
        bool first = true;
        for (const auto& e : scene.data.entities) {
            const int ex = static_cast<int>(std::round(e.x));
            const int ez = static_cast<int>(std::round(e.y));
            if (first) {
                minX = maxX = ex;
                minZ = maxZ = ez;
                first = false;
            } else {
                minX = std::min(minX, ex);
                maxX = std::max(maxX, ex);
                minZ = std::min(minZ, ez);
                maxZ = std::max(maxZ, ez);
            }
        }
    }

    minX -= 3;
    maxX += 3;
    minZ -= 3;
    maxZ += 3;
    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            const bool checker = ((x + z) & 1) == 0;
            RenderInstance tile;
            tile.position = {static_cast<float>(x), -1.05f, static_cast<float>(z)};
            tile.scale = {0.48f, 0.03f, 0.48f};
            tile.color = checker ? dash::physics::Vec3{0.24f, 0.34f, 0.24f}
                                 : dash::physics::Vec3{0.18f, 0.28f, 0.18f};
            out.push_back(std::move(tile));
        }
    }
    return out;
}

} // namespace dash::vkexp
