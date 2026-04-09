#pragma once

#include <cstdint>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

class SqliteStatement {
public:
    SqliteStatement() = default;
    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;
    SqliteStatement(SqliteStatement&& other) noexcept;
    SqliteStatement& operator=(SqliteStatement&& other) noexcept;
    ~SqliteStatement();

    bool isValid() const { return stmt_ != nullptr; }

    bool bindInt(int index, int value);
    bool bindInt64(int index, std::int64_t value);
    bool bindDouble(int index, double value);
    bool bindText(int index, const std::string& value);
    bool bindNull(int index);

    int step();
    bool reset();
    bool clearBindings();

    int columnInt(int col) const;
    std::int64_t columnInt64(int col) const;
    double columnDouble(int col) const;
    std::string columnText(int col) const;

private:
    friend class SqliteDb;
    SqliteStatement(sqlite3* db, sqlite3_stmt* stmt);

    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};
