#include "SaveGame.h"
#include "SaveVersioning.h"
#include "AppPaths.h"
#include "db/DbMode.h"
#include "db/SchemaManager.h"
#include "db/SqliteDb.h"
#include "db/SqliteStatement.h"
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <filesystem>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

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

static json saveDataToJson(const SaveData& data)
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
    return j;
}

static bool saveDataFromJson(const json& j, SaveData& out)
{
    if (!j.is_object()) return false;

    int version = j.value("saveVersion", 0);
    if (version < 1) return false;

    json migrated = j;
    if (!SaveVersioning::migrate(migrated, version)) return false;

    SaveData data;
    data.saveVersion = SaveData::kCurrentVersion;
    data.worldSeed   = migrated.value("worldSeed", 12345u);
    data.score       = migrated.value("score", 0);

    if (migrated.contains("player") && migrated["player"].is_object())
        data.player = playerFromJson(migrated["player"]);

    if (migrated.contains("enemies") && migrated["enemies"].is_array()) {
        for (auto& ej : migrated["enemies"])
            data.enemies.push_back(enemyFromJson(ej));
    }

    out = std::move(data);
    return true;
}

static bool saveLegacyJson(const SaveData& data, const std::string& path)
{
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << saveDataToJson(data).dump(2);
    return file.good();
}

static bool loadLegacyJson(const std::string& path, SaveData& out)
{
    std::ifstream file(path);
    if (!file.is_open()) return false;

    json j;
    try { file >> j; } catch (...) { return false; }
    return saveDataFromJson(j, out);
}

static std::string migrationsDirPath()
{
#ifdef PROJECT_DIR
    return (fs::path(PROJECT_DIR) / "src" / "core" / "db" / "migrations").string();
#else
    return (fs::path("src") / "core" / "db" / "migrations").string();
#endif
}

static std::string sqliteDbPath()
{
    fs::path p;
    if (AppPaths::hasActiveProjectPaths()) {
        p = fs::path(AppPaths::getLibraryDir()) / "dash_engine.db";
    } else {
        p = fs::path(AppPaths::getSavesDir()) / "dash_engine.db";
    }
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    return p.string();
}

static std::string slotNameFromPath(const std::string& path)
{
    fs::path p(path);
    const std::string stem = p.stem().string();
    return stem.empty() ? std::string("default") : stem;
}

static bool ensureSaveSchema(SqliteDb& db)
{
    return SchemaManager::applyMigrations(db, migrationsDirPath(), nullptr);
}

static bool saveSqlite(const SaveData& data, const std::string& path)
{
    SqliteDb db;
    std::string error;
    if (!db.open(sqliteDbPath(), &error)) return false;
    if (!ensureSaveSchema(db)) return false;

    const std::string slotName = slotNameFromPath(path);
    const std::string rawJson = saveDataToJson(data).dump();

    auto stmt = db.prepare(
        "INSERT OR REPLACE INTO savegames(save_id, slot_name, save_version, raw_json, updated_at) "
        "VALUES(?, ?, ?, ?, strftime('%s','now'));",
        &error);
    if (!stmt.isValid()) return false;

    if (!stmt.bindText(1, slotName)
        || !stmt.bindText(2, slotName)
        || !stmt.bindInt(3, SaveData::kCurrentVersion)
        || !stmt.bindText(4, rawJson)) {
        return false;
    }

    return stmt.step() == SQLITE_DONE;
}

static bool loadSqlite(const std::string& path, SaveData& out)
{
    SqliteDb db;
    std::string error;
    if (!db.open(sqliteDbPath(), &error)) return false;
    if (!ensureSaveSchema(db)) return false;

    const std::string slotName = slotNameFromPath(path);
    auto stmt = db.prepare(
        "SELECT raw_json FROM savegames WHERE slot_name = ? LIMIT 1;",
        &error);
    if (!stmt.isValid()) return false;
    if (!stmt.bindText(1, slotName)) return false;

    const int rc = stmt.step();
    if (rc != SQLITE_ROW) return false;

    json j;
    try {
        j = json::parse(stmt.columnText(0));
    } catch (...) {
        return false;
    }

    return saveDataFromJson(j, out);
}

// ─────────────────────────────────────────────────────────────────────────────
// SaveGame::save
// ─────────────────────────────────────────────────────────────────────────────
bool SaveGame::save(const SaveData& data, const std::string& path)
{
    const DbMode::Mode mode = DbMode::current();

    if (mode == DbMode::Mode::Json) {
        return saveLegacyJson(data, path);
    }

    if (saveSqlite(data, path)) {
        return true;
    }

    return DbMode::allowsJsonFallback(mode) ? saveLegacyJson(data, path) : false;
}

// ─────────────────────────────────────────────────────────────────────────────
// SaveGame::load
// ─────────────────────────────────────────────────────────────────────────────
bool SaveGame::load(const std::string& path, SaveData& out)
{
    const DbMode::Mode mode = DbMode::current();

    if (mode == DbMode::Mode::Json) {
        return loadLegacyJson(path, out);
    }

    if (loadSqlite(path, out)) {
        return true;
    }

    return DbMode::allowsJsonFallback(mode) ? loadLegacyJson(path, out) : false;
}
