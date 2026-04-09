#include "scene/SceneRepositorySqlite.h"

#include "db/SchemaManager.h"
#include "db/SqliteDb.h"
#include "db/SqliteStatement.h"

#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {

std::mutex& schemaCacheMutex()
{
    static std::mutex m;
    return m;
}

std::unordered_set<std::string>& schemaCache()
{
    static std::unordered_set<std::string> s;
    return s;
}

std::string migrationsDirPath()
{
#ifdef PROJECT_DIR
    return (fs::path(PROJECT_DIR) / "src" / "core" / "db" / "migrations").string();
#else
    return (fs::path("src") / "core" / "db" / "migrations").string();
#endif
}

std::string sceneIdFromFileName(const std::string& fileName)
{
    return fs::path(fileName).stem().string();
}

} // namespace

SceneRepositorySqlite::SceneRepositorySqlite(std::string dbPath)
    : dbPath_(std::move(dbPath))
{
}

bool SceneRepositorySqlite::ensureSchema(std::string* error) const
{
    {
        std::lock_guard<std::mutex> lock(schemaCacheMutex());
        if (schemaCache().find(dbPath_) != schemaCache().end()) {
            return true;
        }
    }

    SqliteDb db;
    if (!db.open(dbPath_, error)) {
        return false;
    }

    if (!SchemaManager::applyMigrations(db, migrationsDirPath(), nullptr)) {
        if (error) *error = "failed to apply schema migrations";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(schemaCacheMutex());
        schemaCache().insert(dbPath_);
    }

    return true;
}

bool SceneRepositorySqlite::listSceneFiles(std::vector<std::string>& outFiles, std::string* error) const
{
    const auto t0 = std::chrono::steady_clock::now();
    outFiles.clear();
    if (!ensureSchema(error)) {
        return false;
    }

    SqliteDb db;
    if (!db.open(dbPath_, error)) {
        return false;
    }

    auto stmt = db.prepare("SELECT file_name FROM scenes ORDER BY file_name ASC;", error);
    if (!stmt.isValid()) {
        return false;
    }

    while (true) {
        const int rc = stmt.step();
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            if (error) *error = "failed iterating scenes rows";
            return false;
        }
        outFiles.push_back(stmt.columnText(0));
    }

    const auto elapsedMs = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    (void)elapsedMs;

    return true;
}

bool SceneRepositorySqlite::saveScene(const std::string& fileName,
                                      const SceneData& scene,
                                      std::string* error) const
{
    if (!ensureSchema(error)) {
        return false;
    }

    std::string rawJson;
    if (!scene.saveToJsonString(rawJson)) {
        if (error) *error = "failed to serialize scene json";
        return false;
    }

    const std::string sceneId = sceneIdFromFileName(fileName);
    const std::string sceneName = scene.sceneName.empty() ? sceneId : scene.sceneName;

    SqliteDb db;
    if (!db.open(dbPath_, error)) {
        return false;
    }

    auto stmt = db.prepare(
        "INSERT OR REPLACE INTO scenes("
        "scene_id, file_name, scene_name, world_seed, next_entity_id, scene_version, raw_json, updated_at"
        ") VALUES(?, ?, ?, ?, ?, ?, ?, strftime('%s','now'));",
        error);
    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindText(1, sceneId)
        || !stmt.bindText(2, fileName)
        || !stmt.bindText(3, sceneName)
        || !stmt.bindInt64(4, static_cast<std::int64_t>(scene.worldSeed))
        || !stmt.bindInt64(5, static_cast<std::int64_t>(scene.nextEntityId))
        || !stmt.bindInt(6, scene.sceneVersion)
        || !stmt.bindText(7, rawJson)) {
        if (error) *error = "failed binding scene values";
        return false;
    }

    if (stmt.step() != SQLITE_DONE) {
        if (error) *error = "failed to upsert scene row";
        return false;
    }

    return true;
}

bool SceneRepositorySqlite::loadScene(const std::string& fileName,
                                      SceneData& outScene,
                                      const std::string& assetsRoot,
                                      std::string* error) const
{
    if (!ensureSchema(error)) {
        return false;
    }

    SqliteDb db;
    if (!db.open(dbPath_, error)) {
        return false;
    }

    auto stmt = db.prepare(
        "SELECT raw_json FROM scenes WHERE file_name = ? LIMIT 1;",
        error);
    if (!stmt.isValid()) {
        return false;
    }

    if (!stmt.bindText(1, fileName)) {
        if (error) *error = "failed binding file_name";
        return false;
    }

    const int rc = stmt.step();
    if (rc == SQLITE_DONE) {
        if (error) *error = "scene not found";
        return false;
    }
    if (rc != SQLITE_ROW) {
        if (error) *error = "failed loading scene row";
        return false;
    }

    const std::string rawJson = stmt.columnText(0);
    if (!outScene.loadFromJsonString(rawJson, assetsRoot)) {
        if (error) *error = "failed parsing scene json from sqlite";
        return false;
    }

    return true;
}
