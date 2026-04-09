#include "db/SqliteDb.h"

#include <sqlite3.h>

SqliteDb::SqliteDb(SqliteDb&& other) noexcept
    : db_(other.db_)
{
    other.db_ = nullptr;
}

SqliteDb& SqliteDb::operator=(SqliteDb&& other) noexcept
{
    if (this == &other) return *this;
    close();
    db_ = other.db_;
    other.db_ = nullptr;
    return *this;
}

SqliteDb::~SqliteDb()
{
    close();
}

bool SqliteDb::open(const std::string& path, std::string* error)
{
    close();

    sqlite3* handle = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(),
                                   &handle,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                                   nullptr);
    if (rc != SQLITE_OK) {
        if (error) {
            *error = handle ? sqlite3_errmsg(handle) : "sqlite open failed";
        }
        if (handle) sqlite3_close(handle);
        return false;
    }

    db_ = handle;

    if (!exec("PRAGMA foreign_keys = ON;", error)) {
        close();
        return false;
    }

    return true;
}

void SqliteDb::close()
{
    if (!db_) return;
    sqlite3_close(db_);
    db_ = nullptr;
}

bool SqliteDb::exec(const std::string& sql, std::string* error) const
{
    if (!db_) {
        if (error) *error = "database is not open";
        return false;
    }

    char* errMsg = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        if (error) {
            *error = errMsg ? errMsg : sqlite3_errmsg(db_);
        }
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    return true;
}

SqliteStatement SqliteDb::prepare(const std::string& sql, std::string* error) const
{
    if (!db_) {
        if (error) *error = "database is not open";
        return SqliteStatement{};
    }

    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        if (error) *error = sqlite3_errmsg(db_);
        if (stmt) sqlite3_finalize(stmt);
        return SqliteStatement{};
    }

    return SqliteStatement(db_, stmt);
}

bool SqliteDb::beginTransaction(std::string* error) const
{
    return exec("BEGIN IMMEDIATE;", error);
}

bool SqliteDb::commit(std::string* error) const
{
    return exec("COMMIT;", error);
}

bool SqliteDb::rollback(std::string* error) const
{
    return exec("ROLLBACK;", error);
}

std::string SqliteDb::lastError() const
{
    return db_ ? sqlite3_errmsg(db_) : "database is not open";
}
