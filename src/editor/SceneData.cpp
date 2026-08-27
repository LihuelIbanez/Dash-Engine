#include "SceneData.h"
#include "ComponentSerialization.h"
#include "PrefabAsset.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstring>

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
    render3d = Render3DSettings{};
    nextEntityId = 1;
    tileOverrides.clear();
    vertexHeightOverrides.clear();
    cliffOverrides.clear();
    textureOverrides.clear();
    waterBodies.clear();
    entities.clear();

    EntityData player;
    player.id        = allocateEntityId();
    player.type      = EntityData::Type::Player;
    player.name      = "Hero";
    player.x         = WORLD_W / 2.f;
    player.y         = WORLD_H / 2.f;
    player.charClass = "Warrior";
    player.components = {
        TransformComponent{player.x, player.y, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
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
nlohmann::json SceneData::toJson() const
{
    json j;
    j["sceneVersion"] = kCurrentVersion;
    j["name"]         = sceneName;
    j["worldSeed"]    = worldSeed;
    j["render3d"] = {
        {"useVulkan3D", render3d.useVulkan3D},
        {"embeddedPreview", render3d.embeddedPreview},
        {"isoYawDeg", render3d.isoYawDeg},
        {"isoPitchDeg", render3d.isoPitchDeg},
        {"cameraDistance", render3d.cameraDistance},
        {"cameraHeight", render3d.cameraHeight},
        {"zoom", render3d.zoom},
        {"heightScale", render3d.heightScale},
        {"gridOpacity", render3d.gridOpacity}
    };
    j["nextEntityId"] = nextEntityId;

    json tilesArr = json::array();
    for (auto& t : tileOverrides) {
        tilesArr.push_back({
            {"x", t.x}, {"y", t.y},
            {"type", t.tileType}, {"walkable", t.walkable}
        });
    }
    j["tileOverrides"] = tilesArr;

    // Vertex height overrides (v4+)
    json heightArr = json::array();
    for (auto& vh : vertexHeightOverrides) {
        heightArr.push_back({
            {"vx", vh.vx}, {"vy", vh.vy}, {"height", vh.height}
        });
    }
    j["vertexHeightOverrides"] = heightArr;

    // Cliff overrides (v5+)
    json cliffArr = json::array();
    for (auto& co : cliffOverrides) {
        cliffArr.push_back({
            {"vx", co.vx}, {"vy", co.vy}, {"cliffLevel", co.cliffLevel}
        });
    }
    j["cliffOverrides"] = cliffArr;

    // Texture overrides (v5+)
    json texArr = json::array();
    for (auto& to : textureOverrides) {
        texArr.push_back({
            {"vx", to.vx}, {"vy", to.vy},
            {"texIndices", {to.texIndices[0], to.texIndices[1], to.texIndices[2], to.texIndices[3]}},
            {"texWeights", {to.texWeights[0], to.texWeights[1], to.texWeights[2], to.texWeights[3]}}
        });
    }
    j["textureOverrides"] = texArr;

    // Water bodies (v5+)
    json waterArr = json::array();
    for (auto& wb : waterBodies) {
        waterArr.push_back({
            {"id", wb.id}, {"waterLevel", wb.waterLevel},
            {"opacity", wb.opacity},
            {"tint", {wb.tint[0], wb.tint[1], wb.tint[2]}}
        });
    }
    j["waterBodies"] = waterArr;

    json entsArr = json::array();
    for (auto& e : entities) {
        json ej;
        ej["id"]   = e.id;
        ej["type"] = (e.type == EntityData::Type::Player) ? "Player" : "Enemy";
        ej["name"] = e.name;

        // TransformComponent (when present) is the authoritative position source.
        float ex = e.x, ey = e.y;
        for (auto& c : e.components) {
            if (auto* tf = std::get_if<TransformComponent>(&c)) {
                ex = tf->x; ey = tf->y;
                break;
            }
        }
        ej["x"] = ex;
        ej["y"] = ey;
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

    return j;
}

bool SceneData::saveToJsonString(std::string& outJson) const
{
    try {
        outJson = toJson().dump(2);
    } catch (...) {
        return false;
    }
    return true;
}

bool SceneData::saveToFile(const std::string& path)
{
    std::string rawJson;
    if (!saveToJsonString(rawJson)) {
        return false;
    }

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << rawJson;
    filePath = path;
    modified = false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool SceneData::loadFromJson(const nlohmann::json& j, const std::string& assetsRoot)
{
    loadErrors.clear();

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

    if (j.contains("render3d") && j["render3d"].is_object()) {
        const auto& r3 = j["render3d"];
        render3d.useVulkan3D = r3.value("useVulkan3D", true);
        render3d.embeddedPreview = r3.value("embeddedPreview", false);
        render3d.isoYawDeg = r3.value("isoYawDeg", 45.0f);
        render3d.isoPitchDeg = r3.value("isoPitchDeg", 35.264f);
        render3d.cameraDistance = r3.value("cameraDistance", 8.0f);
        render3d.cameraHeight = r3.value("cameraHeight", 2.5f);
        render3d.zoom = r3.value("zoom", 1.0f);
        render3d.heightScale = r3.value("heightScale", 32.0f);
        render3d.gridOpacity = r3.value("gridOpacity", 0.22f);
    } else {
        render3d = Render3DSettings{};
    }

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

    // ── Vertex height overrides (v4+) ───────────────────────────────────────
    vertexHeightOverrides.clear();
    if (j.contains("vertexHeightOverrides") && j["vertexHeightOverrides"].is_array()) {
        constexpr int VW = WORLD_W + 1;
        constexpr int VH = WORLD_H + 1;
        for (auto& vh : j["vertexHeightOverrides"]) {
            if (!vh.is_object()) continue;
            int vx = vh.value("vx", -1);
            int vy = vh.value("vy", -1);
            if (vx < 0 || vx >= VW || vy < 0 || vy >= VH) continue;
            float h = vh.value("height", 0.0f);
            vertexHeightOverrides.push_back({vx, vy, h});
        }
    }

    // ── Cliff overrides (v5+) ───────────────────────────────────────────────
    cliffOverrides.clear();
    if (j.contains("cliffOverrides") && j["cliffOverrides"].is_array()) {
        constexpr int VW = WORLD_W + 1;
        constexpr int VH = WORLD_H + 1;
        for (auto& co : j["cliffOverrides"]) {
            if (!co.is_object()) continue;
            int vx = co.value("vx", -1);
            int vy = co.value("vy", -1);
            if (vx < 0 || vx >= VW || vy < 0 || vy >= VH) continue;
            uint8_t cl = static_cast<uint8_t>(co.value("cliffLevel", 0));
            if (cl > MAX_CLIFF_LEVEL) cl = MAX_CLIFF_LEVEL;
            cliffOverrides.push_back({vx, vy, cl});
        }
    }

    // ── Texture overrides (v5+) ─────────────────────────────────────────────
    textureOverrides.clear();
    if (j.contains("textureOverrides") && j["textureOverrides"].is_array()) {
        constexpr int VW = WORLD_W + 1;
        constexpr int VH = WORLD_H + 1;
        for (auto& to : j["textureOverrides"]) {
            if (!to.is_object()) continue;
            int vx = to.value("vx", -1);
            int vy = to.value("vy", -1);
            if (vx < 0 || vx >= VW || vy < 0 || vy >= VH) continue;
            TextureOverride txo;
            txo.vx = vx; txo.vy = vy;
            if (to.contains("texIndices") && to["texIndices"].is_array() && to["texIndices"].size() == 4) {
                for (int i = 0; i < 4; ++i)
                    txo.texIndices[i] = static_cast<uint8_t>(to["texIndices"][i].get<int>());
            } else {
                std::memset(txo.texIndices, 0, 4);
            }
            if (to.contains("texWeights") && to["texWeights"].is_array() && to["texWeights"].size() == 4) {
                for (int i = 0; i < 4; ++i)
                    txo.texWeights[i] = static_cast<uint8_t>(to["texWeights"][i].get<int>());
            } else {
                txo.texWeights[0] = 255; txo.texWeights[1] = 0;
                txo.texWeights[2] = 0; txo.texWeights[3] = 0;
            }
            textureOverrides.push_back(txo);
        }
    }

    // ── Water bodies (v5+) ──────────────────────────────────────────────────
    waterBodies.clear();
    if (j.contains("waterBodies") && j["waterBodies"].is_array()) {
        for (auto& wb : j["waterBodies"]) {
            if (!wb.is_object()) continue;
            WaterBody body;
            body.id = static_cast<uint8_t>(wb.value("id", 0));
            body.waterLevel = wb.value("waterLevel", 0.3f);
            body.opacity = wb.value("opacity", 0.6f);
            if (wb.contains("tint") && wb["tint"].is_array() && wb["tint"].size() == 3) {
                body.tint[0] = wb["tint"][0].get<float>();
                body.tint[1] = wb["tint"][1].get<float>();
                body.tint[2] = wb["tint"][2].get<float>();
            }
            waterBodies.push_back(body);
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
        for (auto& comp : ed.components) {
            if (auto* tf = std::get_if<TransformComponent>(&comp)) {
                // TransformComponent is authoritative; mirror it into the legacy
                // x/y fields (not the other way around) so direct component edits
                // survive a save/load round-trip, and re-clamp in case it drifted.
                tf->x = std::max(0.f, std::min((float)(WORLD_W - 1), tf->x));
                tf->y = std::max(0.f, std::min((float)(WORLD_H - 1), tf->y));
                ed.x = tf->x;
                ed.y = tf->y;
                if (sceneVersion < 3) {
                    tf->z = 0.0f;
                    tf->scale = 1.0f;
                }
            }
            if (auto* rc = std::get_if<RenderComponent>(&comp)) {
                if (sceneVersion < 3) {
                    rc->renderMode = static_cast<int>(RenderMode::Mesh3D);
                    if (rc->mesh.empty()) rc->mesh = "cube";
                    if (rc->material.empty()) rc->material = "default";
                }
            }
        }
        if (ed.id >= nextEntityId)
            nextEntityId = ed.id + 1;
    }

    modified = false;
    return true;
}

bool SceneData::loadFromJsonString(const std::string& rawJson, const std::string& assetsRoot)
{
    json j;
    try {
        j = json::parse(rawJson);
    } catch (const json::parse_error& e) {
        loadErrors.clear();
        loadErrors.push_back("JSON parse error: " + std::string(e.what()));
        return false;
    } catch (...) {
        loadErrors.clear();
        loadErrors.push_back("Unknown error parsing JSON");
        return false;
    }

    return loadFromJson(j, assetsRoot);
}

bool SceneData::loadFromFile(const std::string& path, const std::string& assetsRoot)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        loadErrors.clear();
        loadErrors.push_back("Cannot open file: " + path);
        return false;
    }

    json j;
    try { file >> j; }
    catch (const json::parse_error& e) {
        loadErrors.clear();
        loadErrors.push_back("JSON parse error: " + std::string(e.what()));
        return false;
    }
    catch (...) {
        loadErrors.clear();
        loadErrors.push_back("Unknown error parsing JSON");
        return false;
    }

    if (!loadFromJson(j, assetsRoot)) {
        return false;
    }

    filePath = path;
    return true;
}
