#include "SceneData.h"
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
    entities.push_back(player);

    modified = false;
    filePath.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
bool SceneData::saveToFile(const std::string& path)
{
    json j;
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
bool SceneData::loadFromFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) return false;

    json j;
    try { file >> j; }
    catch (...) { return false; }

    sceneName    = j.value("name", "Untitled");
    worldSeed    = j.value("worldSeed", 12345u);
    nextEntityId = j.value("nextEntityId", (uint64_t)1);

    tileOverrides.clear();
    if (j.contains("tileOverrides")) {
        for (auto& t : j["tileOverrides"]) {
            tileOverrides.push_back({
                t.value("x", 0), t.value("y", 0),
                t.value("type", 0), t.value("walkable", true)
            });
        }
    }

    entities.clear();
    if (j.contains("entities")) {
        for (auto& e : j["entities"]) {
            EntityData ed;
            std::string typeStr = e.value("type", "Enemy");
            ed.type      = (typeStr == "Player") ? EntityData::Type::Player
                                                 : EntityData::Type::Enemy;
            ed.name      = e.value("name", "Entity");
            ed.x         = e.value("x", 0.f);
            ed.y         = e.value("y", 0.f);
            ed.charClass = e.value("class", "Warrior");

            // Backward compat: assign ID if missing from old scene files
            if (e.contains("id"))
                ed.id = e["id"].get<uint64_t>();
            else
                ed.id = allocateEntityId();

            entities.push_back(ed);
        }
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
