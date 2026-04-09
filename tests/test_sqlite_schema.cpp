#include "db/SchemaManager.h"
#include "db/SqliteDb.h"

#include <sqlite3.h>

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { \
        ++g_pass; \
    } \
} while(0)

static bool tableExists(SqliteDb& db, const std::string& table)
{
    std::string err;
    auto stmt = db.prepare("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?;", &err);
    if (!stmt.isValid() || !stmt.bindText(1, table)) return false;
    if (stmt.step() != SQLITE_ROW) return false;
    return stmt.columnInt(0) == 1;
}

int main()
{
    std::printf("=== test_sqlite_schema ===\n");

    const fs::path base = fs::temp_directory_path() / "dash_test_sqlite_schema";
    const fs::path dbPath = base / "schema.db";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);

    SqliteDb db;
    std::string error;
    ASSERT(db.open(dbPath.string(), &error), "open sqlite db");

#ifdef PROJECT_DIR
    const fs::path migrationsDir = fs::path(PROJECT_DIR) / "src" / "core" / "db" / "migrations";
#else
    const fs::path migrationsDir = fs::path("src") / "core" / "db" / "migrations";
#endif

    ASSERT(SchemaManager::applyMigrations(db, migrationsDir.string(), nullptr), "apply migrations first time");
    ASSERT(SchemaManager::applyMigrations(db, migrationsDir.string(), nullptr), "apply migrations second time (idempotent)");

    ASSERT(tableExists(db, "schema_migrations"), "schema_migrations exists");
    ASSERT(tableExists(db, "assets"), "assets exists");
    ASSERT(tableExists(db, "player_classes"), "player_classes exists");
    ASSERT(tableExists(db, "scenes"), "scenes exists");
    ASSERT(tableExists(db, "savegames"), "savegames exists");

    fs::remove_all(base, ec);
    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
