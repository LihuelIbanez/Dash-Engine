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
    ok &= loadItems(dir + "items.json");
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

const ItemData* GameplayDatabase::findItem(const std::string& id) const
{
    auto it = itemIndex_.find(id);
    return it != itemIndex_.end() ? &items_[it->second] : nullptr;
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
        && loadLootTablesFromSqlite()
        && loadItemsFromSqlite();
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

namespace {

ItemData itemFromJson(const json& j)
{
    ItemData d;
    d.id          = j.value("id", "unknown");
    d.name        = j.value("name", d.id);
    d.description = j.value("description", "");
    d.type        = itemTypeFromStr(j.value("type", "misc"));
    d.rarity      = itemRarityFromStr(j.value("rarity", "normal"));
    d.icon        = j.value("icon", "");
    d.levelReq    = j.value("levelReq", 1);
    d.goldValue   = j.value("goldValue", 0);
    d.stackable   = j.value("stackable", false);
    d.maxStack    = j.value("maxStack", 1);
    d.bonusAttack      = j.value("bonusAttack", 0);
    d.bonusDefense     = j.value("bonusDefense", 0);
    d.bonusMagicAttack = j.value("bonusMagicAttack", 0);
    d.bonusSpeed       = j.value("bonusSpeed", 0.0f);
    d.bonusCritChance  = j.value("bonusCritChance", 0.0f);
    d.bonusMaxHp       = j.value("bonusMaxHp", 0);
    d.bonusMaxMana     = j.value("bonusMaxMana", 0);
    d.consumableEffect = j.value("consumableEffect", "");
    d.consumableValue  = j.value("consumableValue", 0);
    return d;
}

json itemToJson(const ItemData& d)
{
    json j;
    j["id"]          = d.id;
    j["name"]        = d.name;
    j["description"] = d.description;
    j["type"]        = itemTypeToStr(d.type);
    j["rarity"]      = itemRarityToStr(d.rarity);
    j["icon"]        = d.icon;
    j["levelReq"]    = d.levelReq;
    j["goldValue"]   = d.goldValue;
    j["stackable"]   = d.stackable;
    j["maxStack"]    = d.maxStack;
    if (d.bonusAttack != 0)      j["bonusAttack"] = d.bonusAttack;
    if (d.bonusDefense != 0)     j["bonusDefense"] = d.bonusDefense;
    if (d.bonusMagicAttack != 0) j["bonusMagicAttack"] = d.bonusMagicAttack;
    if (d.bonusSpeed != 0.0f)      j["bonusSpeed"] = d.bonusSpeed;
    if (d.bonusCritChance != 0.0f) j["bonusCritChance"] = d.bonusCritChance;
    if (d.bonusMaxHp != 0)   j["bonusMaxHp"] = d.bonusMaxHp;
    if (d.bonusMaxMana != 0) j["bonusMaxMana"] = d.bonusMaxMana;
    if (!d.consumableEffect.empty()) {
        j["consumableEffect"] = d.consumableEffect;
        j["consumableValue"]  = d.consumableValue;
    }
    return j;
}

} // namespace

bool GameplayDatabase::loadItems(const std::string& path)
{
    // items.json is a newer, optional gameplay file: older/test projects that
    // predate it should still load successfully with an empty item catalog.
    std::ifstream f(path);
    if (!f.is_open()) {
        std::printf("[GameplayDB] No items.json at %s (0 items)\n", path.c_str());
        return true;
    }

    json arr = json::parse(f, nullptr, false);
    if (arr.is_discarded() || !arr.is_array()) {
        std::fprintf(stderr, "[GameplayDB] Invalid JSON in %s\n", path.c_str());
        return false;
    }

    for (auto& j : arr) {
        ItemData d = itemFromJson(j);
        itemIndex_[d.id] = items_.size();
        items_.push_back(std::move(d));
    }

    std::printf("[GameplayDB] Loaded %zu items\n", items_.size());
    return true;
}

bool GameplayDatabase::loadItemsFromSqlite()
{
    SqliteDb db;
    std::string error;
    if (!db.open(sqlitePath_, &error)) {
        return false;
    }

    auto stmt = db.prepare(
        "SELECT id, name, description, item_type, rarity, icon, level_req, gold_value, "
        "stackable, max_stack, bonus_attack, bonus_defense, bonus_magic_attack, "
        "bonus_speed, bonus_crit_chance, bonus_max_hp, bonus_max_mana, "
        "consumable_effect, consumable_value FROM items;",
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

        ItemData d;
        d.id          = stmt.columnText(0);
        d.name        = stmt.columnText(1);
        d.description = stmt.columnText(2);
        d.type        = itemTypeFromStr(stmt.columnText(3));
        d.rarity      = itemRarityFromStr(stmt.columnText(4));
        d.icon        = stmt.columnText(5);
        d.levelReq    = stmt.columnInt(6);
        d.goldValue   = stmt.columnInt(7);
        d.stackable   = stmt.columnInt(8) != 0;
        d.maxStack    = stmt.columnInt(9);
        d.bonusAttack      = stmt.columnInt(10);
        d.bonusDefense     = stmt.columnInt(11);
        d.bonusMagicAttack = stmt.columnInt(12);
        d.bonusSpeed       = static_cast<float>(stmt.columnDouble(13));
        d.bonusCritChance  = static_cast<float>(stmt.columnDouble(14));
        d.bonusMaxHp       = stmt.columnInt(15);
        d.bonusMaxMana     = stmt.columnInt(16);
        d.consumableEffect = stmt.columnText(17);
        d.consumableValue  = stmt.columnInt(18);

        itemIndex_[d.id] = items_.size();
        items_.push_back(std::move(d));
    }

    std::printf("[GameplayDB] Loaded %zu items (SQLite)\n", items_.size());
    return true;
}

void GameplayDatabase::rebuildItemIndex()
{
    itemIndex_.clear();
    for (std::size_t i = 0; i < items_.size(); ++i)
        itemIndex_[items_[i].id] = i;
}

bool GameplayDatabase::saveItemsToJson(const std::string& path) const
{
    json arr = json::array();
    for (const ItemData& d : items_) arr.push_back(itemToJson(d));

    std::ofstream out(path);
    if (!out.is_open()) {
        std::fprintf(stderr, "[GameplayDB] Cannot write %s\n", path.c_str());
        return false;
    }
    out << arr.dump(4) << '\n';
    return true;
}

void GameplayDatabase::rebuildEnemyIndex()
{
    enemyIndex_.clear();
    for (std::size_t i = 0; i < enemies_.size(); ++i)
        enemyIndex_[enemies_[i].id] = i;
}

bool GameplayDatabase::saveEnemiesToJson(const std::string& path) const
{
    json arr = json::array();
    for (const EnemyData& d : enemies_) {
        json j;
        j["id"]              = d.id;
        j["name"]            = d.name;
        j["maxHp"]           = d.maxHp;
        j["detectionRadius"] = d.detectionRadius;
        j["attackRadius"]    = d.attackRadius;
        j["expReward"]       = d.expReward;
        j["attackCooldown"]  = d.attackCooldown;
        j["stats"] = {
            { "attack",      d.attack },
            { "defense",     d.defense },
            { "magicAttack", d.magicAttack },
            { "speed",       d.speed },
            { "critChance",  d.critChance },
        };
        arr.push_back(std::move(j));
    }

    std::ofstream out(path);
    if (!out.is_open()) {
        std::fprintf(stderr, "[GameplayDB] Cannot write %s\n", path.c_str());
        return false;
    }
    out << arr.dump(4) << '\n';
    return true;
}

void GameplayDatabase::rebuildClassIndex()
{
    classIndex_.clear();
    for (std::size_t i = 0; i < playerClasses_.size(); ++i)
        classIndex_[playerClasses_[i].id] = i;
}

bool GameplayDatabase::savePlayerClassesToJson(const std::string& path) const
{
    json arr = json::array();
    for (const PlayerClassData& d : playerClasses_) {
        json j;
        j["id"]              = d.id;
        j["name"]            = d.name;
        j["description"]     = d.description;
        j["maxHp"]           = d.maxHp;
        j["maxMana"]         = d.maxMana;
        j["attackCooldown"]  = d.attackCooldown;
        j["stats"] = {
            { "attack",      d.attack },
            { "defense",     d.defense },
            { "magicAttack", d.magicAttack },
            { "speed",       d.speed },
            { "critChance",  d.critChance },
        };
        arr.push_back(std::move(j));
    }

    std::ofstream out(path);
    if (!out.is_open()) {
        std::fprintf(stderr, "[GameplayDB] Cannot write %s\n", path.c_str());
        return false;
    }
    out << arr.dump(4) << '\n';
    return true;
}

void GameplayDatabase::clearAll()
{
    playerClasses_.clear();
    enemies_.clear();
    lootTables_.clear();
    items_.clear();
    classIndex_.clear();
    enemyIndex_.clear();
    lootByEnemy_.clear();
    itemIndex_.clear();
}
