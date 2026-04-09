#include "project/ProjectDataMigrator.h"

#include "db/SchemaManager.h"
#include "db/SqliteDb.h"
#include "db/SqliteStatement.h"
#include "SceneData.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>

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

void logLine(ProjectDataMigrator::Result& res, const std::string& msg)
{
    res.log.push_back(msg);
}

void logError(ProjectDataMigrator::Result& res, const std::string& msg)
{
    ++res.summary.errorCount;
    res.log.push_back(msg);
}

bool readJsonArray(const fs::path& path, json& out)
{
    std::ifstream in(path);
    if (!in.is_open()) return false;

    out = json::parse(in, nullptr, false);
    return !out.is_discarded() && out.is_array();
}

bool clearGameplayTables(SqliteDb& db, std::string* error)
{
    return db.exec("DELETE FROM loot_drops;", error)
        && db.exec("DELETE FROM loot_table_enemies;", error)
        && db.exec("DELETE FROM loot_tables;", error)
        && db.exec("DELETE FROM enemies;", error)
        && db.exec("DELETE FROM player_classes;", error);
}

bool clearSceneTables(SqliteDb& db, std::string* error)
{
    return db.exec("DELETE FROM scenes;", error);
}

bool migratePlayerClasses(const fs::path& gameplayDir, SqliteDb& db, ProjectDataMigrator::Result& res)
{
    json arr;
    if (!readJsonArray(gameplayDir / "player_classes.json", arr)) {
        logError(res, "[Migrator] Missing or invalid player_classes.json");
        return false;
    }

    std::string error;
    auto stmt = db.prepare(
        "INSERT OR REPLACE INTO player_classes("
        "id, name, description, max_hp, max_mana, attack_cooldown,"
        "attack, defense, magic_attack, speed, crit_chance"
        ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
        &error);
    if (!stmt.isValid()) {
        logError(res, "[Migrator] Could not prepare player_classes insert: " + error);
        return false;
    }

    for (const auto& item : arr) {
        const auto& stats = item.contains("stats") ? item["stats"] : json::object();
        if (!stmt.reset() || !stmt.clearBindings()
            || !stmt.bindText(1, item.value("id", "unknown"))
            || !stmt.bindText(2, item.value("name", "unknown"))
            || !stmt.bindText(3, item.value("description", ""))
            || !stmt.bindInt(4, item.value("maxHp", 100))
            || !stmt.bindInt(5, item.value("maxMana", 50))
            || !stmt.bindDouble(6, item.value("attackCooldown", 1.0))
            || !stmt.bindInt(7, stats.value("attack", 10))
            || !stmt.bindInt(8, stats.value("defense", 5))
            || !stmt.bindInt(9, stats.value("magicAttack", 0))
            || !stmt.bindDouble(10, stats.value("speed", 3.0))
            || !stmt.bindDouble(11, stats.value("critChance", 0.05))) {
            logError(res, "[Migrator] Could not bind player_classes row");
            return false;
        }

        if (stmt.step() != SQLITE_DONE) {
            logError(res, "[Migrator] Could not insert player_classes row");
            return false;
        }
    }

    res.summary.playerClasses = static_cast<int>(arr.size());
    logLine(res, "[Migrator] Migrated player classes: " + std::to_string(arr.size()));
    return true;
}

bool migrateEnemies(const fs::path& gameplayDir, SqliteDb& db, ProjectDataMigrator::Result& res)
{
    json arr;
    if (!readJsonArray(gameplayDir / "enemies.json", arr)) {
        logError(res, "[Migrator] Missing or invalid enemies.json");
        return false;
    }

    std::string error;
    auto stmt = db.prepare(
        "INSERT OR REPLACE INTO enemies("
        "id, name, max_hp, detection_radius, attack_radius, exp_reward, attack_cooldown,"
        "attack, defense, magic_attack, speed, crit_chance"
        ") VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
        &error);
    if (!stmt.isValid()) {
        logError(res, "[Migrator] Could not prepare enemies insert: " + error);
        return false;
    }

    for (const auto& item : arr) {
        const auto& stats = item.contains("stats") ? item["stats"] : json::object();
        if (!stmt.reset() || !stmt.clearBindings()
            || !stmt.bindText(1, item.value("id", "unknown"))
            || !stmt.bindText(2, item.value("name", "unknown"))
            || !stmt.bindInt(3, item.value("maxHp", 60))
            || !stmt.bindDouble(4, item.value("detectionRadius", 6.0))
            || !stmt.bindDouble(5, item.value("attackRadius", 1.2))
            || !stmt.bindInt(6, item.value("expReward", 40))
            || !stmt.bindDouble(7, item.value("attackCooldown", 1.0))
            || !stmt.bindInt(8, stats.value("attack", 10))
            || !stmt.bindInt(9, stats.value("defense", 5))
            || !stmt.bindInt(10, stats.value("magicAttack", 0))
            || !stmt.bindDouble(11, stats.value("speed", 2.5))
            || !stmt.bindDouble(12, stats.value("critChance", 0.05))) {
            logError(res, "[Migrator] Could not bind enemies row");
            return false;
        }

        if (stmt.step() != SQLITE_DONE) {
            logError(res, "[Migrator] Could not insert enemies row");
            return false;
        }
    }

    res.summary.enemies = static_cast<int>(arr.size());
    logLine(res, "[Migrator] Migrated enemies: " + std::to_string(arr.size()));
    return true;
}

bool migrateLoot(const fs::path& gameplayDir, SqliteDb& db, ProjectDataMigrator::Result& res)
{
    json arr;
    if (!readJsonArray(gameplayDir / "loot_tables.json", arr)) {
        logError(res, "[Migrator] Missing or invalid loot_tables.json");
        return false;
    }

    std::string error;
    auto insertLoot = db.prepare("INSERT OR REPLACE INTO loot_tables(id) VALUES(?);", &error);
    auto insertEnemy = db.prepare(
        "INSERT OR REPLACE INTO loot_table_enemies(loot_id, enemy_id) VALUES(?, ?);",
        &error);
    auto insertDrop = db.prepare(
        "INSERT OR REPLACE INTO loot_drops(loot_id, item, chance, min_qty, max_qty) VALUES(?, ?, ?, ?, ?);",
        &error);

    if (!insertLoot.isValid() || !insertEnemy.isValid() || !insertDrop.isValid()) {
        logError(res, "[Migrator] Could not prepare loot insert statements: " + error);
        return false;
    }

    std::size_t enemyLinks = 0;
    std::size_t drops = 0;
    for (const auto& item : arr) {
        const std::string lootId = item.value("id", "unknown");

        if (!insertLoot.reset() || !insertLoot.clearBindings() || !insertLoot.bindText(1, lootId)
            || insertLoot.step() != SQLITE_DONE) {
            logError(res, "[Migrator] Could not insert loot table row");
            return false;
        }

        if (item.contains("enemies") && item["enemies"].is_array()) {
            for (const auto& enemy : item["enemies"]) {
                if (!insertEnemy.reset() || !insertEnemy.clearBindings()
                    || !insertEnemy.bindText(1, lootId)
                    || !insertEnemy.bindText(2, enemy.get<std::string>())
                    || insertEnemy.step() != SQLITE_DONE) {
                    logError(res, "[Migrator] Could not insert loot_table_enemies row");
                    return false;
                }
                ++enemyLinks;
            }
        }

        if (item.contains("drops") && item["drops"].is_array()) {
            for (const auto& drop : item["drops"]) {
                if (!insertDrop.reset() || !insertDrop.clearBindings()
                    || !insertDrop.bindText(1, lootId)
                    || !insertDrop.bindText(2, drop.value("item", "unknown"))
                    || !insertDrop.bindDouble(3, drop.value("chance", 0.0))
                    || !insertDrop.bindInt(4, drop.value("minQty", 1))
                    || !insertDrop.bindInt(5, drop.value("maxQty", 1))
                    || insertDrop.step() != SQLITE_DONE) {
                    logError(res, "[Migrator] Could not insert loot_drops row");
                    return false;
                }
                ++drops;
            }
        }
    }

    res.summary.lootTables = static_cast<int>(arr.size());
    res.summary.lootEnemyLinks = static_cast<int>(enemyLinks);
    res.summary.lootDrops = static_cast<int>(drops);
    logLine(res, "[Migrator] Migrated loot tables: " + std::to_string(arr.size()));
    logLine(res, "[Migrator] Migrated loot enemy links: " + std::to_string(enemyLinks));
    logLine(res, "[Migrator] Migrated loot drops: " + std::to_string(drops));
    return true;
}

bool migrateAssetDb(const fs::path& assetsDir, SqliteDb& db, ProjectDataMigrator::Result& res)
{
    const fs::path path = assetsDir / "asset_db.json";
    std::ifstream in(path);
    if (!in.is_open()) {
        logLine(res, "[Migrator] asset_db.json not found, skipping assets migration");
        return true;
    }

    json root = json::parse(in, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("assets") || !root["assets"].is_array()) {
        logError(res, "[Migrator] Invalid asset_db.json format");
        return false;
    }

    std::string error;
    if (!db.exec("DELETE FROM asset_dependencies;", &error)
        || !db.exec("DELETE FROM assets;", &error)) {
        logError(res, "[Migrator] Could not clear assets tables: " + error);
        return false;
    }

    auto insertAsset = db.prepare(
        "INSERT OR REPLACE INTO assets(guid, source_path, import_path, asset_type, hash, last_import_time) "
        "VALUES(?, ?, ?, ?, ?, ?);",
        &error);
    auto insertDep = db.prepare(
        "INSERT OR REPLACE INTO asset_dependencies(asset_guid, dependency_path) VALUES(?, ?);",
        &error);
    if (!insertAsset.isValid() || !insertDep.isValid()) {
        logError(res, "[Migrator] Could not prepare assets insert statements: " + error);
        return false;
    }

    std::size_t depCount = 0;
    for (const auto& item : root["assets"]) {
        const std::string guid = item.value("guid", "");
        if (guid.empty()) continue;

        if (!insertAsset.reset() || !insertAsset.clearBindings()
            || !insertAsset.bindText(1, guid)
            || !insertAsset.bindText(2, item.value("sourcePath", ""))
            || !insertAsset.bindText(3, item.value("importPath", ""))
            || !insertAsset.bindText(4, item.value("assetType", "Unknown"))
            || !insertAsset.bindText(5, item.value("hash", ""))
            || !insertAsset.bindInt64(6, item.value("lastImportTime", std::int64_t(0)))
            || insertAsset.step() != SQLITE_DONE) {
            logError(res, "[Migrator] Could not insert assets row");
            return false;
        }

        if (item.contains("dependencies") && item["dependencies"].is_array()) {
            for (const auto& dep : item["dependencies"]) {
                if (!insertDep.reset() || !insertDep.clearBindings()
                    || !insertDep.bindText(1, guid)
                    || !insertDep.bindText(2, dep.get<std::string>())
                    || insertDep.step() != SQLITE_DONE) {
                    logError(res, "[Migrator] Could not insert asset dependency row");
                    return false;
                }
                ++depCount;
            }
        }
    }

    res.summary.assets = static_cast<int>(root["assets"].size());
    res.summary.assetDependencies = static_cast<int>(depCount);
    logLine(res, "[Migrator] Migrated assets: " + std::to_string(root["assets"].size()));
    logLine(res, "[Migrator] Migrated asset dependencies: " + std::to_string(depCount));
    return true;
}

bool migrateScenes(const fs::path& scenesDir,
                   const fs::path& assetsDir,
                   SqliteDb& db,
                   ProjectDataMigrator::Result& res)
{
    std::error_code ec;
    if (!fs::exists(scenesDir, ec) || !fs::is_directory(scenesDir, ec)) {
        logLine(res, "[Migrator] scenes directory not found, skipping scenes migration");
        return true;
    }

    std::string error;
    auto insertScene = db.prepare(
        "INSERT OR REPLACE INTO scenes("
        "scene_id, file_name, scene_name, world_seed, next_entity_id, scene_version, raw_json, updated_at"
        ") VALUES(?, ?, ?, ?, ?, ?, ?, strftime('%s','now'));",
        &error);
    if (!insertScene.isValid()) {
        logError(res, "[Migrator] Could not prepare scenes insert statement: " + error);
        return false;
    }

    int count = 0;
    for (const auto& entry : fs::directory_iterator(scenesDir, ec)) {
        if (ec) {
            logError(res, "[Migrator] Error reading scenes directory");
            return false;
        }
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;

        SceneData scene;
        if (!scene.loadFromFile(entry.path().string(), assetsDir.string())) {
            logError(res, "[Migrator] Failed loading scene for migration: " + entry.path().filename().string());
            return false;
        }

        std::string rawJson;
        if (!scene.saveToJsonString(rawJson)) {
            logError(res, "[Migrator] Failed serializing scene json: " + entry.path().filename().string());
            return false;
        }

        const std::string fileName = entry.path().filename().string();
        const std::string sceneId = entry.path().stem().string();
        const std::string sceneName = scene.sceneName.empty() ? sceneId : scene.sceneName;

        if (!insertScene.reset() || !insertScene.clearBindings()
            || !insertScene.bindText(1, sceneId)
            || !insertScene.bindText(2, fileName)
            || !insertScene.bindText(3, sceneName)
            || !insertScene.bindInt64(4, static_cast<std::int64_t>(scene.worldSeed))
            || !insertScene.bindInt64(5, static_cast<std::int64_t>(scene.nextEntityId))
            || !insertScene.bindInt(6, scene.sceneVersion)
            || !insertScene.bindText(7, rawJson)
            || insertScene.step() != SQLITE_DONE) {
            logError(res, "[Migrator] Failed inserting scene row: " + fileName);
            return false;
        }

        ++count;
    }

    res.summary.scenes = count;
    logLine(res, "[Migrator] Migrated scenes: " + std::to_string(count));
    return true;
}

bool backupExistingDb(const fs::path& dbPath, ProjectDataMigrator::Result& res)
{
    std::error_code ec;
    if (!fs::exists(dbPath, ec) || ec) {
        return true;
    }

    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    fs::path backupPath = dbPath;
    backupPath += ".bak." + std::to_string(static_cast<long long>(now));

    fs::copy_file(dbPath, backupPath, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        logError(res, "[Migrator] Could not create DB backup: " + backupPath.string());
        return false;
    }

    logLine(res, "[Migrator] DB backup created: " + backupPath.string());
    return true;
}

bool runIntegrityChecks(SqliteDb& db, ProjectDataMigrator::Result& res)
{
    std::string error;

    auto integrity = db.prepare("PRAGMA integrity_check;", &error);
    if (!integrity.isValid()) {
        logError(res, "[Migrator] Could not run integrity_check: " + error);
        return false;
    }
    const int irc = integrity.step();
    if (irc != SQLITE_ROW || integrity.columnText(0) != "ok") {
        logError(res, "[Migrator] integrity_check failed");
        return false;
    }

    auto fk = db.prepare("PRAGMA foreign_key_check;", &error);
    if (!fk.isValid()) {
        logError(res, "[Migrator] Could not run foreign_key_check: " + error);
        return false;
    }
    const int fkRc = fk.step();
    if (fkRc != SQLITE_DONE) {
        logError(res, "[Migrator] foreign_key_check reported violations");
        return false;
    }

    logLine(res, "[Migrator] Integrity checks passed.");
    return true;
}

} // namespace

ProjectDataMigrator::Result ProjectDataMigrator::migrateJsonToSqlite(const ProjectManifest& manifest)
{
    Result res;
    const auto t0 = std::chrono::steady_clock::now();

    auto finish = [&res, &t0]() {
        const auto t1 = std::chrono::steady_clock::now();
        res.summary.elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    };

    const fs::path libraryDir = manifest.absoluteLibraryDir();
    const fs::path assetsDir = manifest.absoluteAssetsDir();
    const fs::path gameplayDir = assetsDir / "gameplay";
    const fs::path scenesDir = manifest.absoluteScenesDir();
    const fs::path dbPath = libraryDir / "dash_engine.db";

    res.dbPath = dbPath.string();
    logLine(res, "[Migrator] Start JSON -> SQLite migration for project: " + manifest.name);

    std::error_code ec;
    fs::create_directories(libraryDir, ec);
    if (ec) {
        logError(res, "[Migrator] Could not create library directory: " + libraryDir.string());
        finish();
        return res;
    }

    if (!backupExistingDb(dbPath, res)) {
        finish();
        return res;
    }

    SqliteDb db;
    std::string error;
    if (!db.open(dbPath.string(), &error)) {
        logError(res, "[Migrator] Could not open SQLite DB: " + error);
        finish();
        return res;
    }

    if (!SchemaManager::applyMigrations(db, migrationsDirPath(), &res.log)) {
        logError(res, "[Migrator] Failed applying schema migrations.");
        finish();
        return res;
    }

    if (!db.beginTransaction(&error)) {
        logError(res, "[Migrator] Could not start transaction: " + error);
        finish();
        return res;
    }

    if (!migrateAssetDb(assetsDir, db, res)
        || !clearGameplayTables(db, &error)
        || !clearSceneTables(db, &error)
        || !migratePlayerClasses(gameplayDir, db, res)
        || !migrateEnemies(gameplayDir, db, res)
        || !migrateLoot(gameplayDir, db, res)
        || !migrateScenes(scenesDir, assetsDir, db, res)) {
        db.rollback(nullptr);
        logError(res, "[Migrator] Migration failed. Transaction rolled back.");
        finish();
        return res;
    }

    if (!db.commit(&error)) {
        db.rollback(nullptr);
        logError(res, "[Migrator] Could not commit transaction: " + error);
        finish();
        return res;
    }

    if (!runIntegrityChecks(db, res)) {
        finish();
        return res;
    }

    res.success = true;
    logLine(res, "[Migrator] Migration completed successfully.");
    finish();
    return res;
}
