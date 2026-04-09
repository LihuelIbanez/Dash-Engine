#include "AssetRepositorySqlite.h"

#include "db/SchemaManager.h"
#include "db/SqliteDb.h"
#include "db/SqliteStatement.h"

#include <sqlite3.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

std::string migrationsDirPath()
{
#ifdef PROJECT_DIR
    return (fs::path(PROJECT_DIR) / "src" / "core" / "db" / "migrations").string();
#else
    return (fs::path("src") / "core" / "db" / "migrations").string();
#endif
}

std::string typeToString(AssetType type)
{
    switch (type) {
        case AssetType::Texture:        return "Texture";
        case AssetType::TileSet:        return "TileSet";
        case AssetType::Scene:          return "Scene";
        case AssetType::GameplayConfig: return "GameplayConfig";
        default:                        return "Unknown";
    }
}

AssetType stringToType(const std::string& type)
{
    if (type == "Texture") return AssetType::Texture;
    if (type == "TileSet") return AssetType::TileSet;
    if (type == "Scene") return AssetType::Scene;
    if (type == "GameplayConfig") return AssetType::GameplayConfig;
    return AssetType::Unknown;
}

} // namespace

bool AssetRepositorySqlite::load(const std::string& dbPath,
                                 std::unordered_map<std::string, AssetRecord>& outRecords,
                                 std::string* error)
{
    outRecords.clear();

    SqliteDb db;
    if (!db.open(dbPath, error)) {
        return false;
    }

    if (!SchemaManager::applyMigrations(db, migrationsDirPath(), nullptr)) {
        if (error) *error = "failed to apply schema migrations";
        return false;
    }

    auto loadStmt = db.prepare(
        "SELECT guid, source_path, import_path, asset_type, hash, last_import_time FROM assets;",
        error);
    if (!loadStmt.isValid()) {
        return false;
    }

    while (true) {
        const int rc = loadStmt.step();
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            if (error) *error = "failed to iterate assets rows";
            return false;
        }

        AssetRecord rec;
        rec.guid = loadStmt.columnText(0);
        rec.sourcePath = loadStmt.columnText(1);
        rec.importPath = loadStmt.columnText(2);
        rec.assetType = stringToType(loadStmt.columnText(3));
        rec.hash = loadStmt.columnText(4);
        rec.lastImportTime = loadStmt.columnInt64(5);
        outRecords[rec.guid] = std::move(rec);
    }

    auto depStmt = db.prepare(
        "SELECT asset_guid, dependency_path FROM asset_dependencies;",
        error);
    if (!depStmt.isValid()) {
        return false;
    }

    while (true) {
        const int rc = depStmt.step();
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            if (error) *error = "failed to iterate dependency rows";
            return false;
        }

        const std::string guid = depStmt.columnText(0);
        const std::string dep = depStmt.columnText(1);
        auto it = outRecords.find(guid);
        if (it != outRecords.end()) {
            it->second.dependencies.push_back(dep);
        }
    }

    return true;
}

bool AssetRepositorySqlite::save(const std::string& dbPath,
                                 const std::unordered_map<std::string, AssetRecord>& records,
                                 std::string* error)
{
    SqliteDb db;
    if (!db.open(dbPath, error)) {
        return false;
    }

    if (!SchemaManager::applyMigrations(db, migrationsDirPath(), nullptr)) {
        if (error) *error = "failed to apply schema migrations";
        return false;
    }

    if (!db.beginTransaction(error)) {
        return false;
    }

    auto rollback = [&]() {
        db.rollback(nullptr);
    };

    if (!db.exec("DELETE FROM asset_dependencies;", error)) {
        rollback();
        return false;
    }
    if (!db.exec("DELETE FROM assets;", error)) {
        rollback();
        return false;
    }

    auto insertAsset = db.prepare(
        "INSERT INTO assets(guid, source_path, import_path, asset_type, hash, last_import_time) "
        "VALUES(?, ?, ?, ?, ?, ?);",
        error);
    if (!insertAsset.isValid()) {
        rollback();
        return false;
    }

    auto insertDep = db.prepare(
        "INSERT INTO asset_dependencies(asset_guid, dependency_path) VALUES(?, ?);",
        error);
    if (!insertDep.isValid()) {
        rollback();
        return false;
    }

    for (const auto& item : records) {
        const AssetRecord& rec = item.second;

        if (!insertAsset.reset() || !insertAsset.clearBindings()
            || !insertAsset.bindText(1, rec.guid)
            || !insertAsset.bindText(2, rec.sourcePath)
            || !insertAsset.bindText(3, rec.importPath)
            || !insertAsset.bindText(4, typeToString(rec.assetType))
            || !insertAsset.bindText(5, rec.hash)
            || !insertAsset.bindInt64(6, rec.lastImportTime)) {
            if (error) *error = "failed to bind asset insert values";
            rollback();
            return false;
        }

        if (insertAsset.step() != SQLITE_DONE) {
            if (error) *error = "failed to insert asset row";
            rollback();
            return false;
        }

        for (const auto& dep : rec.dependencies) {
            if (!insertDep.reset() || !insertDep.clearBindings()
                || !insertDep.bindText(1, rec.guid)
                || !insertDep.bindText(2, dep)) {
                if (error) *error = "failed to bind dependency insert values";
                rollback();
                return false;
            }

            if (insertDep.step() != SQLITE_DONE) {
                if (error) *error = "failed to insert dependency row";
                rollback();
                return false;
            }
        }
    }

    if (!db.commit(error)) {
        rollback();
        return false;
    }

    return true;
}
