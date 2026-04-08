#include "SceneData.h"
#include "ComponentSerialization.h"
#include "PrefabAsset.h"
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
uint64_t SceneData::allocateEntityId()
{
    return nextEntityId++;
}

// ─────────────────────────────────────────────────────────────────────────────
void SceneData::createDefault()
{
    sceneName = "Untitled";
    worldSeed = 12345;
    nextEntityId = 1;
    tileOverrides.clear();
    entities.clear();

    EntityData player;
    player.id        = allocateEntityId();
    player.type      = EntityData::Type::Player;
    player.name      = "Hero";
    player.x         = WORLD_W / 2.f;
    player.y         = WORLD_H / 2.f;
    player.charClass = "Warrior";
    player.components = {
        TransformComponent{player.x, player.y},
        HealthComponent{100, 100},
        StatsComponent{15, 10, 0, 3},   // Warrior defaults
        ManaComponent{50, 50},
        RenderComponent{}
    };
    entities.push_back(player);

    modified = false;
    filePath.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
bool SceneData::saveToFile(const std::string& path)
{
    json j;
    j["sceneVersion"] = kCurrentVersion;
    j["name"]         = sceneName;
    j["worldSeed"]    = worldSeed;
    j["nextEntityId"] = nextEntityId;

    json tilesArr = json::array();
    for (auto& t : tileOverrides) {
        tilesArr.push_back({
            {"x", t.x}, {"y", t.y},
            {"type", t.tileType}, {"walkable", t.walkable}
        });
    }
    j["tileOverrides"] = tilesArr;

    json entsArr = json::array();
    for (auto& e : entities) {
        json ej;
        ej["id"]   = e.id;
        ej["type"] = (e.type == EntityData::Type::Player) ? "Player" : "Enemy";
        ej["name"] = e.name;
        ej["x"]    = e.x;
        ej["y"]    = e.y;
        if (e.type == EntityData::Type::Player)
            ej["class"] = e.charClass;
        if (!e.prefabGuid.empty()) {
            // Prefab instance: store only GUID + overrides.
            ej["prefabGuid"] = e.prefabGuid;
            ej["overrides"]  = e.componentOverrides.is_null()
                               ? nlohmann::json::object()
                               : e.componentOverrides;
        } else if (!e.components.empty()) {
            json compsArr = json::array();
            for (auto& c : e.components)
                compsArr.push_back(componentToJson(c));
            ej["components"] = compsArr;
        }
        entsArr.push_back(ej);
    }
    j["entities"] = entsArr;

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << j.dump(2);
    filePath = path;
    modified = false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool SceneData::loadFromFile(const std::string& path, const std::string& assetsRoot)
{
    loadErrors.clear();

    std::ifstream file(path);
    if (!file.is_open()) {
        loadErrors.push_back("Cannot open file: " + path);
        return false;
    }

    json j;
    try { file >> j; }
    catch (const json::parse_error& e) {
        loadErrors.push_back("JSON parse error: " + std::string(e.what()));
        return false;
    }
    catch (...) {
        loadErrors.push_back("Unknown error parsing JSON");
        return false;
    }

    if (!j.is_object()) {
        loadErrors.push_back("Root is not a JSON object");
        return false;
    }

    // ── Version check ────────────────────────────────────────────────────────
    sceneVersion = j.value("sceneVersion", 0);
    if (sceneVersion > kCurrentVersion) {
        loadErrors.push_back("Scene version " + std::to_string(sceneVersion)
                           + " is newer than supported (" + std::to_string(kCurrentVersion) + ")");
        return false;
    }
    if (sceneVersion == 0) {
        loadErrors.push_back("Warning: scene has no version field, treating as legacy (v0)");
    }

    // ── Core fields ──────────────────────────────────────────────────────────
    sceneName    = j.value("name", "Untitled");
    worldSeed    = j.value("worldSeed", 12345u);
    nextEntityId = j.value("nextEntityId", (uint64_t)1);

    // ── Tile overrides ───────────────────────────────────────────────────────
    tileOverrides.clear();
    if (j.contains("tileOverrides")) {
        if (!j["tileOverrides"].is_array()) {
            loadErrors.push_back("'tileOverrides' is not an array, skipping");
        } else {
            int idx = 0;
            for (auto& t : j["tileOverrides"]) {
                if (!t.is_object()) {
                    loadErrors.push_back("tileOverrides[" + std::to_string(idx) + "] is not an object, skipping");
                    ++idx; continue;
                }
                int tx = t.value("x", -1);
                int ty = t.value("y", -1);
                if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) {
                    loadErrors.push_back("tileOverrides[" + std::to_string(idx) + "] out of bounds ("
                                       + std::to_string(tx) + "," + std::to_string(ty) + "), skipping");
                    ++idx; continue;
                }
                int tileType = t.value("type", 0);
                if (tileType < 0 || tileType > 8) {
                    loadErrors.push_back("tileOverrides[" + std::to_string(idx) + "] invalid type "
                                       + std::to_string(tileType) + ", clamping");
                    tileType = std::max(0, std::min(8, tileType));
                }
                tileOverrides.push_back({tx, ty, tileType, t.value("walkable", true)});
                ++idx;
            }
        }
    }

    // ── Entities ─────────────────────────────────────────────────────────────
    entities.clear();
    bool hasPlayer = false;
    if (j.contains("entities")) {
        if (!j["entities"].is_array()) {
            loadErrors.push_back("'entities' is not an array, skipping");
        } else {
            int idx = 0;
            for (auto& e : j["entities"]) {
                if (!e.is_object()) {
                    loadErrors.push_back("entities[" + std::to_string(idx) + "] is not an object, skipping");
                    ++idx; continue;
                }
                EntityData ed;
                std::string typeStr = e.value("type", "Enemy");
                ed.type = (typeStr == "Player") ? EntityData::Type::Player
                                                : EntityData::Type::Enemy;
                ed.name      = e.value("name", "Entity");
                ed.x         = e.value("x", 0.f);
                ed.y         = e.value("y", 0.f);
                ed.charClass = e.value("class", "Warrior");

                // Validate position bounds
                if (ed.x < 0 || ed.x >= WORLD_W || ed.y < 0 || ed.y >= WORLD_H) {
                    loadErrors.push_back("entities[" + std::to_string(idx) + "] '"
                                       + ed.name + "' position out of bounds, clamping");
                    ed.x = std::max(0.f, std::min((float)(WORLD_W - 1), ed.x));
                    ed.y = std::max(0.f, std::min((float)(WORLD_H - 1), ed.y));
                }

                // Backward compat: assign ID if missing from old scene files
                if (e.contains("id"))
                    ed.id = e["id"].get<uint64_t>();
                else
                    ed.id = allocateEntityId();

                // ── Components: prefab instance, full list, or legacy migration ──
                if (e.contains("prefabGuid") && e["prefabGuid"].is_string()) {
                    // Prefab instance: load base + apply overrides.
                    ed.prefabGuid = e["prefabGuid"].get<std::string>();
                    if (e.contains("overrides"))
                        ed.componentOverrides = e["overrides"];

                    if (!assetsRoot.empty()) {
                        std::string prefabsDir = assetsRoot + "/prefabs";
                        PrefabAsset prefab = findPrefabByGuid(prefabsDir, ed.prefabGuid);
                        if (prefab.guid.empty()) {
                            loadErrors.push_back("entities[" + std::to_string(idx)
                                + "] '" + ed.name + "': prefab '" + ed.prefabGuid
                                + "' not found, using empty components");
                        } else {
                            applyOverrides(prefab, ed.components, ed.componentOverrides);
                        }
                    } else {
                        loadErrors.push_back("entities[" + std::to_string(idx)
                            + "] '" + ed.name + "': prefabGuid set but no assetsRoot provided");
                    }
                } else if (e.contains("components") && e["components"].is_array()) {
                    for (auto& cj : e["components"]) {
                        try {
                            ed.components.push_back(componentFromJson(cj));
                        } catch (...) {
                            loadErrors.push_back("entities[" + std::to_string(idx)
                                + "] '" + ed.name + "': failed to parse a component, skipping");
                        }
                    }
                } else {
                    // Migrate legacy entity → components (v0/v1 or saved without components)
                    ed.components.push_back(TransformComponent{ed.x, ed.y});
                    ed.components.push_back(HealthComponent{100, 100});
                    if (ed.type == EntityData::Type::Player) {
                        StatsComponent stats;
                        if (ed.charClass == "Mage") {
                            stats.attack = 10; stats.defense = 5;
                            stats.magicAttack = 15; stats.speed = 4;
                        } else if (ed.charClass == "Rogue") {
                            stats.attack = 12; stats.defense = 7;
                            stats.speed = 5;
                        } else { // Warrior and default
                            stats.attack = 15; stats.defense = 10;
                            stats.speed = 3;
                        }
                        ed.components.push_back(stats);
                        ed.components.push_back(ManaComponent{
                            ed.charClass == "Mage" ? 100 : 50,
                            ed.charClass == "Mage" ? 100 : 50});
                    } else {
                        ed.components.push_back(StatsComponent{});
                        ed.components.push_back(AIComponent{});
                    }
                    ed.components.push_back(RenderComponent{});
                }

                if (ed.type == EntityData::Type::Player) hasPlayer = true;
                entities.push_back(ed);
                ++idx;
            }
        }
    }

    if (!hasPlayer) {
        loadErrors.push_back("Warning: scene has no Player entity");
    }

    // Ensure nextEntityId is above all loaded IDs
    for (auto& ed : entities) {
        if (ed.id >= nextEntityId)
            nextEntityId = ed.id + 1;
    }

    filePath = path;
    modified = false;
    return true;
}
