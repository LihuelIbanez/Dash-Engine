#pragma once

#include "db/SqliteStatement.h"

#include <string>

struct sqlite3;

class SqliteDb {
public:
    SqliteDb() = default;
    SqliteDb(const SqliteDb&) = delete;
    SqliteDb& operator=(const SqliteDb&) = delete;
    SqliteDb(SqliteDb&& other) noexcept;
    SqliteDb& operator=(SqliteDb&& other) noexcept;
    ~SqliteDb();

    bool open(const std::string& path, std::string* error = nullptr);
    void close();

    bool isOpen() const { return db_ != nullptr; }
    sqlite3* handle() const { return db_; }

    bool exec(const std::string& sql, std::string* error = nullptr) const;
    SqliteStatement prepare(const std::string& sql, std::string* error = nullptr) const;

    bool beginTransaction(std::string* error = nullptr) const;
    bool commit(std::string* error = nullptr) const;
    bool rollback(std::string* error = nullptr) const;

    std::string lastError() const;

private:
    sqlite3* db_ = nullptr;
};
