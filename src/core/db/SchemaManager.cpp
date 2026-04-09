#include "db/SchemaManager.h"

#include "db/SqliteDb.h"
#include "db/SqliteStatement.h"

#include <sqlite3.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace {

void logLine(std::vector<std::string>* log, const std::string& line)
{
    if (log) log->push_back(line);
}

bool ensureMigrationsTable(SqliteDb& db, std::vector<std::string>* log)
{
    std::string error;
    const char* sql =
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "version INTEGER PRIMARY KEY,"
        "applied_at TEXT NOT NULL"
        ");";

    if (!db.exec(sql, &error)) {
        logLine(log, "[Schema] Could not create schema_migrations table: " + error);
        return false;
    }

    return true;
}

bool parseVersionFromFileName(const std::string& filename, int& version)
{
    version = 0;
    std::size_t pos = 0;
    while (pos < filename.size() && std::isdigit(static_cast<unsigned char>(filename[pos]))) {
        version = (version * 10) + (filename[pos] - '0');
        ++pos;
    }

    if (pos == 0 || pos >= filename.size() || filename[pos] != '_') {
        return false;
    }

    return version > 0;
}

bool readSqlFile(const fs::path& path, std::string& outSql)
{
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::ostringstream ss;
    ss << in.rdbuf();
    outSql = ss.str();
    return true;
}

bool loadAppliedVersions(SqliteDb& db, std::set<int>& appliedVersions, std::vector<std::string>* log)
{
    std::string error;
    auto stmt = db.prepare("SELECT version FROM schema_migrations;", &error);
    if (!stmt.isValid()) {
        logLine(log, "[Schema] Could not query applied migrations: " + error);
        return false;
    }

    while (true) {
        const int rc = stmt.step();
        if (rc == SQLITE_ROW) {
            appliedVersions.insert(stmt.columnInt(0));
            continue;
        }
        if (rc == SQLITE_DONE) {
            break;
        }

        logLine(log, "[Schema] Failed while reading applied migrations.");
        return false;
    }

    return true;
}

bool applySingleMigration(SqliteDb& db,
                          int version,
                          const fs::path& path,
                          std::vector<std::string>* log)
{
    std::string sql;
    if (!readSqlFile(path, sql)) {
        logLine(log, "[Schema] Could not read migration file: " + path.string());
        return false;
    }

    std::string error;
    if (!db.beginTransaction(&error)) {
        logLine(log, "[Schema] Could not start migration transaction: " + error);
        return false;
    }

    if (!db.exec(sql, &error)) {
        db.rollback(nullptr);
        logLine(log, "[Schema] Migration failed v" + std::to_string(version) + ": " + error);
        return false;
    }

    auto stmt = db.prepare(
        "INSERT INTO schema_migrations(version, applied_at) VALUES(?, datetime('now'));",
        &error);
    if (!stmt.isValid()) {
        db.rollback(nullptr);
        logLine(log, "[Schema] Could not prepare schema_migrations insert: " + error);
        return false;
    }

    if (!stmt.bindInt(1, version)) {
        db.rollback(nullptr);
        logLine(log, "[Schema] Could not bind migration version for v" + std::to_string(version));
        return false;
    }

    const int stepRc = stmt.step();
    if (stepRc != SQLITE_DONE) {
        db.rollback(nullptr);
        logLine(log, "[Schema] Could not persist migration record for v" + std::to_string(version));
        return false;
    }

    if (!db.commit(&error)) {
        db.rollback(nullptr);
        logLine(log, "[Schema] Could not commit migration v" + std::to_string(version) + ": " + error);
        return false;
    }

    logLine(log, "[Schema] Applied migration v" + std::to_string(version) + " from " + path.filename().string());
    return true;
}

} // namespace

bool SchemaManager::applyMigrations(SqliteDb& db,
                                    const std::string& migrationsDir,
                                    std::vector<std::string>* log)
{
    if (!db.isOpen()) {
        logLine(log, "[Schema] Database is not open.");
        return false;
    }

    if (!ensureMigrationsTable(db, log)) {
        return false;
    }

    std::set<int> appliedVersions;
    if (!loadAppliedVersions(db, appliedVersions, log)) {
        return false;
    }

    std::vector<std::pair<int, fs::path>> migrationFiles;
    std::error_code ec;
    const fs::path dirPath(migrationsDir);
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec)) {
        logLine(log, "[Schema] Migrations directory not found: " + migrationsDir);
        return false;
    }

    for (const auto& entry : fs::directory_iterator(dirPath, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".sql") continue;

        int version = 0;
        if (!parseVersionFromFileName(entry.path().filename().string(), version)) {
            continue;
        }

        migrationFiles.emplace_back(version, entry.path());
    }

    std::sort(migrationFiles.begin(), migrationFiles.end(),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.first < rhs.first;
              });

    for (const auto& item : migrationFiles) {
        const int version = item.first;
        if (appliedVersions.find(version) != appliedVersions.end()) {
            continue;
        }

        if (!applySingleMigration(db, version, item.second, log)) {
            return false;
        }
    }

    logLine(log, "[Schema] Migration pass complete.");
    return true;
}
