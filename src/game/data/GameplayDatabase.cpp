#include "GameplayDatabase.h"
#include "db/DbMode.h"
#include "db/SchemaManager.h"
#include "db/SqliteDb.h"
#include "db/SqliteStatement.h"
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <filesystem>
#include <fstream>
#include <cstdio>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string migrationsDirPath()
{
#ifdef PROJECT_DIR
    return (fs::path(PROJECT_DIR) / "src" / "core" / "db" / "migrations").string();
#else
    return (fs::path("src") / "core" / "db" / "migrations").string();
#endif
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Public
// ─────────────────────────────────────────────────────────────────────────────

bool GameplayDatabase::load(const std::string& assetsDir)
{
    clearAll();
    const DbMode::Mode mode = DbMode::current();

    std::string dir = assetsDir;
    if (!dir.empty() && dir.back() != '/') dir += '/';
    dir += "gameplay/";

    if (DbMode::usesSqliteRead(mode)) {
        const fs::path dbPath = fs::path(assetsDir) / ".." / ".library" / "dash_engine.db";
        if (loadFromSqlite(dbPath.lexically_normal().string())) {
            return true;
        }

        if (!DbMode::allowsJsonFallback(mode)) {
            return false;
        }

        clearAll();
    }

    bool ok = true;
    ok &= loadPlayerClasses(dir + "player_classes.json");
    ok &= loadEnemies(dir + "enemies.json");
    ok &= loadLootTables(dir + "loot_tables.json");
    return ok;
}

const PlayerClassData* GameplayDatabase::findPlayerClass(const std::string& id) const
{
    auto it = classIndex_.find(id);
    return it != classIndex_.end() ? &playerClasses_[it->second] : nullptr;
}

const EnemyData* GameplayDatabase::findEnemy(const std::string& id) const
{
    auto it = enemyIndex_.find(id);
    return it != enemyIndex_.end() ? &enemies_[it->second] : nullptr;
}

const LootTableData* GameplayDatabase::findLootTableForEnemy(const std::string& enemyId) const
{
    auto it = lootByEnemy_.find(enemyId);
    return it != lootByEnemy_.end() ? &lootTables_[it->second] : nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private loaders
// ─────────────────────────────────────────────────────────────────────────────

bool GameplayDatabase::loadPlayerClasses(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "[GameplayDB] Cannot open %s\n", path.c_str());
        return false;
    }

    json arr = json::parse(f, nullptr, false);
    if (arr.is_discarded() || !arr.is_array()) {
        std::fprintf(stderr, "[GameplayDB] Invalid JSON in %s\n", path.c_str());
        return false;
    }

    for (auto& j : arr) {
        PlayerClassData d;
        d.id          = j.value("id",          "unknown");
        d.name        = j.value("name",        d.id);
        d.description = j.value("description", "");
        d.maxHp       = j.value("maxHp",       100);
        d.maxMana     = j.value("maxMana",     50);
        d.attackCooldown = j.value("attackCooldown", 1.0f);
        if (j.contains("stats")) {
            auto& s = j["stats"];
            d.attack      = s.value("attack",      10);
            d.defense     = s.value("defense",      5);
            d.magicAttack = s.value("magicAttack",  0);
            d.speed       = s.value("speed",        3.0f);
            d.critChance  = s.value("critChance",   0.05f);
        }
        classIndex_[d.id] = playerClasses_.size();
        playerClasses_.push_back(std::move(d));
    }

    std::printf("[GameplayDB] Loaded %zu player classes\n", playerClasses_.size());
    return true;
}

bool GameplayDatabase::loadFromSqlite(const std::string& dbPath)
{
    sqlitePath_ = dbPath;
    if (!fs::exists(sqlitePath_)) {
        return false;
    }

    SqliteDb db;
    std::string error;
    if (!db.open(sqlitePath_, &error)) {
        std::fprintf(stderr, "[GameplayDB] Could not open SQLite DB %s: %s\n", sqlitePath_.c_str(), error.c_str());
        return false;
    }

    if (!SchemaManager::applyMigrations(db, migrationsDirPath(), nullptr)) {
        std::fprintf(stderr, "[GameplayDB] Could not apply SQLite migrations for %s\n", sqlitePath_.c_str());
        return false;
    }

    return loadPlayerClassesFromSqlite()
        && loadEnemiesFromSqlite()
        && loadLootTablesFromSqlite();
}

bool GameplayDatabase::loadPlayerClassesFromSqlite()
{
    SqliteDb db;
    std::string error;
    if (!db.open(sqlitePath_, &error)) {
        return false;
    }

    auto stmt = db.prepare(
        "SELECT id, name, description, max_hp, max_mana, attack_cooldown, "
        "attack, defense, magic_attack, speed, crit_chance FROM player_classes;",
        &error);
    if (!stmt.isValid()) {
        return false;
    }

    while (true) {
        const int rc = stmt.step();
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            return false;
        }

        PlayerClassData d;
        d.id = stmt.columnText(0);
        d.name = stmt.columnText(1);
        d.description = stmt.columnText(2);
        d.maxHp = stmt.columnInt(3);
        d.maxMana = stmt.columnInt(4);
        d.attackCooldown = static_cast<float>(stmt.columnDouble(5));
        d.attack = stmt.columnInt(6);
        d.defense = stmt.columnInt(7);
        d.magicAttack = stmt.columnInt(8);
        d.speed = static_cast<float>(stmt.columnDouble(9));
        d.critChance = static_cast<float>(stmt.columnDouble(10));

        classIndex_[d.id] = playerClasses_.size();
        playerClasses_.push_back(std::move(d));
    }

    std::printf("[GameplayDB] Loaded %zu player classes (SQLite)\n", playerClasses_.size());
    return true;
}

bool GameplayDatabase::loadEnemies(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "[GameplayDB] Cannot open %s\n", path.c_str());
        return false;
    }

    json arr = json::parse(f, nullptr, false);
    if (arr.is_discarded() || !arr.is_array()) {
        std::fprintf(stderr, "[GameplayDB] Invalid JSON in %s\n", path.c_str());
        return false;
    }

    for (auto& j : arr) {
        EnemyData d;
        d.id              = j.value("id",              "unknown");
        d.name            = j.value("name",            d.id);
        d.maxHp           = j.value("maxHp",           60);
        d.detectionRadius = j.value("detectionRadius",  6.0f);
        d.attackRadius    = j.value("attackRadius",     1.2f);
        d.expReward       = j.value("expReward",        40);
        d.attackCooldown  = j.value("attackCooldown",   1.0f);
        if (j.contains("stats")) {
            auto& s = j["stats"];
            d.attack      = s.value("attack",      10);
            d.defense     = s.value("defense",      5);
            d.magicAttack = s.value("magicAttack",  0);
            d.speed       = s.value("speed",        2.5f);
            d.critChance  = s.value("critChance",   0.05f);
        }
        enemyIndex_[d.id] = enemies_.size();
        enemies_.push_back(std::move(d));
    }

    std::printf("[GameplayDB] Loaded %zu enemy types\n", enemies_.size());
    return true;
}

bool GameplayDatabase::loadEnemiesFromSqlite()
{
    SqliteDb db;
    std::string error;
    if (!db.open(sqlitePath_, &error)) {
        return false;
    }

    auto stmt = db.prepare(
        "SELECT id, name, max_hp, detection_radius, attack_radius, exp_reward, attack_cooldown, "
        "attack, defense, magic_attack, speed, crit_chance FROM enemies;",
        &error);
    if (!stmt.isValid()) {
        return false;
    }

    while (true) {
        const int rc = stmt.step();
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            return false;
        }

        EnemyData d;
        d.id = stmt.columnText(0);
        d.name = stmt.columnText(1);
        d.maxHp = stmt.columnInt(2);
        d.detectionRadius = static_cast<float>(stmt.columnDouble(3));
        d.attackRadius = static_cast<float>(stmt.columnDouble(4));
        d.expReward = stmt.columnInt(5);
        d.attackCooldown = static_cast<float>(stmt.columnDouble(6));
        d.attack = stmt.columnInt(7);
        d.defense = stmt.columnInt(8);
        d.magicAttack = stmt.columnInt(9);
        d.speed = static_cast<float>(stmt.columnDouble(10));
        d.critChance = static_cast<float>(stmt.columnDouble(11));

        enemyIndex_[d.id] = enemies_.size();
        enemies_.push_back(std::move(d));
    }

    std::printf("[GameplayDB] Loaded %zu enemy types (SQLite)\n", enemies_.size());
    return true;
}

bool GameplayDatabase::loadLootTables(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "[GameplayDB] Cannot open %s\n", path.c_str());
        return false;
    }

    json arr = json::parse(f, nullptr, false);
    if (arr.is_discarded() || !arr.is_array()) {
        std::fprintf(stderr, "[GameplayDB] Invalid JSON in %s\n", path.c_str());
        return false;
    }

    for (auto& j : arr) {
        LootTableData d;
        d.id = j.value("id", "unknown");
        if (j.contains("enemies")) {
            for (auto& eid : j["enemies"])
                d.enemies.push_back(eid.get<std::string>());
        }
        if (j.contains("drops")) {
            for (auto& dj : j["drops"]) {
                LootDrop ld;
                ld.item   = dj.value("item",   "unknown");
                ld.chance = dj.value("chance",  0.0f);
                ld.minQty = dj.value("minQty",  1);
                ld.maxQty = dj.value("maxQty",  1);
                d.drops.push_back(std::move(ld));
            }
        }
        std::size_t idx = lootTables_.size();
        for (auto& eid : d.enemies)
            lootByEnemy_[eid] = idx;
        lootTables_.push_back(std::move(d));
    }

    std::printf("[GameplayDB] Loaded %zu loot tables\n", lootTables_.size());
    return true;
}

bool GameplayDatabase::loadLootTablesFromSqlite()
{
    SqliteDb db;
    std::string error;
    if (!db.open(sqlitePath_, &error)) {
        return false;
    }

    auto lootStmt = db.prepare("SELECT id FROM loot_tables;", &error);
    auto enemyStmt = db.prepare("SELECT enemy_id FROM loot_table_enemies WHERE loot_id = ?;", &error);
    auto dropStmt = db.prepare(
        "SELECT item, chance, min_qty, max_qty FROM loot_drops WHERE loot_id = ?;",
        &error);
    if (!lootStmt.isValid() || !enemyStmt.isValid() || !dropStmt.isValid()) {
        return false;
    }

    while (true) {
        const int rc = lootStmt.step();
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            return false;
        }

        LootTableData d;
        d.id = lootStmt.columnText(0);

        if (!enemyStmt.reset() || !enemyStmt.clearBindings() || !enemyStmt.bindText(1, d.id)) {
            return false;
        }
        while (true) {
            const int erc = enemyStmt.step();
            if (erc == SQLITE_DONE) break;
            if (erc != SQLITE_ROW) {
                return false;
            }
            d.enemies.push_back(enemyStmt.columnText(0));
        }

        if (!dropStmt.reset() || !dropStmt.clearBindings() || !dropStmt.bindText(1, d.id)) {
            return false;
        }
        while (true) {
            const int drc = dropStmt.step();
            if (drc == SQLITE_DONE) break;
            if (drc != SQLITE_ROW) {
                return false;
            }

            LootDrop drop;
            drop.item = dropStmt.columnText(0);
            drop.chance = static_cast<float>(dropStmt.columnDouble(1));
            drop.minQty = dropStmt.columnInt(2);
            drop.maxQty = dropStmt.columnInt(3);
            d.drops.push_back(std::move(drop));
        }

        const std::size_t idx = lootTables_.size();
        for (const auto& eid : d.enemies) {
            lootByEnemy_[eid] = idx;
        }
        lootTables_.push_back(std::move(d));
    }

    std::printf("[GameplayDB] Loaded %zu loot tables (SQLite)\n", lootTables_.size());
    return true;
}

void GameplayDatabase::clearAll()
{
    playerClasses_.clear();
    enemies_.clear();
    lootTables_.clear();
    classIndex_.clear();
    enemyIndex_.clear();
    lootByEnemy_.clear();
}
