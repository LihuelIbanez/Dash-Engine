#include "SaveGame.h"
#include "SaveVersioning.h"
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Serialisation helpers
// ─────────────────────────────────────────────────────────────────────────────
static json playerToJson(const SavePlayerData& p)
{
    return {
        {"x",            p.x},
        {"y",            p.y},
        {"health",       p.health},
        {"maxHealth",    p.maxHealth},
        {"mana",         p.mana},
        {"maxMana",      p.maxMana},
        {"attack",       p.attack},
        {"defense",      p.defense},
        {"magicAttack",  p.magicAttack},
        {"speed",        p.speed},
        {"critChance",   p.critChance},
        {"level",        p.level},
        {"experience",   p.experience},
        {"expToNext",    p.expToNext},
        {"atkCooldownMax", p.atkCooldownMax},
        {"class",        p.charClass}
    };
}

static SavePlayerData playerFromJson(const json& j)
{
    SavePlayerData p;
    p.x            = j.value("x",            0.f);
    p.y            = j.value("y",            0.f);
    p.health       = j.value("health",       100);
    p.maxHealth    = j.value("maxHealth",    100);
    p.mana         = j.value("mana",         50);
    p.maxMana      = j.value("maxMana",      50);
    p.attack       = j.value("attack",       10);
    p.defense      = j.value("defense",      5);
    p.magicAttack  = j.value("magicAttack",  0);
    p.speed        = j.value("speed",        3.f);
    p.critChance   = j.value("critChance",   0.05f);
    p.level        = j.value("level",        1);
    p.experience   = j.value("experience",   0);
    p.expToNext    = j.value("expToNext",    100);
    p.atkCooldownMax = j.value("atkCooldownMax", 1.f);
    p.charClass    = j.value("class",        std::string("Warrior"));
    return p;
}

static json enemyToJson(const SaveEnemyData& e)
{
    return {
        {"x",               e.x},
        {"y",               e.y},
        {"health",          e.health},
        {"maxHealth",       e.maxHealth},
        {"alive",           e.alive},
        {"name",            e.name},
        {"attack",          e.attack},
        {"defense",         e.defense},
        {"magicAttack",     e.magicAttack},
        {"speed",           e.speed},
        {"critChance",      e.critChance},
        {"detectionRadius", e.detectionRadius},
        {"attackRadius",    e.attackRadius},
        {"expReward",       e.expReward},
        {"atkCooldownMax",  e.atkCooldownMax}
    };
}

static SaveEnemyData enemyFromJson(const json& j)
{
    SaveEnemyData e;
    e.x               = j.value("x",               0.f);
    e.y               = j.value("y",               0.f);
    e.health          = j.value("health",          60);
    e.maxHealth       = j.value("maxHealth",       60);
    e.alive           = j.value("alive",           true);
    e.name            = j.value("name",            std::string("Enemy"));
    e.attack          = j.value("attack",          10);
    e.defense         = j.value("defense",         5);
    e.magicAttack     = j.value("magicAttack",     0);
    e.speed           = j.value("speed",           2.5f);
    e.critChance      = j.value("critChance",      0.05f);
    e.detectionRadius = j.value("detectionRadius", 6.f);
    e.attackRadius    = j.value("attackRadius",    1.2f);
    e.expReward       = j.value("expReward",       40);
    e.atkCooldownMax  = j.value("atkCooldownMax",  1.f);
    return e;
}

// ─────────────────────────────────────────────────────────────────────────────
// SaveGame::save
// ─────────────────────────────────────────────────────────────────────────────
bool SaveGame::save(const SaveData& data, const std::string& path)
{
    json j;
    j["saveVersion"] = data.saveVersion;
    j["worldSeed"]   = data.worldSeed;
    j["score"]       = data.score;
    j["player"]      = playerToJson(data.player);

    json arr = json::array();
    for (auto& e : data.enemies)
        arr.push_back(enemyToJson(e));
    j["enemies"] = arr;

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << j.dump(2);
    return file.good();
}

// ─────────────────────────────────────────────────────────────────────────────
// SaveGame::load
// ─────────────────────────────────────────────────────────────────────────────
bool SaveGame::load(const std::string& path, SaveData& out)
{
    std::ifstream file(path);
    if (!file.is_open()) return false;

    json j;
    try { file >> j; } catch (...) { return false; }
    if (!j.is_object()) return false;

    int version = j.value("saveVersion", 0);
    if (version < 1) return false;

    // Migrate old versions if needed
    if (!SaveVersioning::migrate(j, version)) return false;

    SaveData data;
    data.saveVersion = SaveData::kCurrentVersion;
    data.worldSeed   = j.value("worldSeed", 12345u);
    data.score       = j.value("score", 0);

    if (j.contains("player") && j["player"].is_object())
        data.player = playerFromJson(j["player"]);

    if (j.contains("enemies") && j["enemies"].is_array()) {
        for (auto& ej : j["enemies"])
            data.enemies.push_back(enemyFromJson(ej));
    }

    out = std::move(data);
    return true;
}
