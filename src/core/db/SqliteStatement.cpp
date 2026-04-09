#include "db/SqliteStatement.h"

#include <sqlite3.h>

SqliteStatement::SqliteStatement(sqlite3* db, sqlite3_stmt* stmt)
    : db_(db), stmt_(stmt)
{
}

SqliteStatement::SqliteStatement(SqliteStatement&& other) noexcept
    : db_(other.db_), stmt_(other.stmt_)
{
    other.db_ = nullptr;
    other.stmt_ = nullptr;
}

SqliteStatement& SqliteStatement::operator=(SqliteStatement&& other) noexcept
{
    if (this == &other) return *this;
    if (stmt_) sqlite3_finalize(stmt_);

    db_ = other.db_;
    stmt_ = other.stmt_;
    other.db_ = nullptr;
    other.stmt_ = nullptr;
    return *this;
}

SqliteStatement::~SqliteStatement()
{
    if (stmt_) sqlite3_finalize(stmt_);
}

bool SqliteStatement::bindInt(int index, int value)
{
    return stmt_ && sqlite3_bind_int(stmt_, index, value) == SQLITE_OK;
}

bool SqliteStatement::bindInt64(int index, std::int64_t value)
{
    return stmt_ && sqlite3_bind_int64(stmt_, index, value) == SQLITE_OK;
}

bool SqliteStatement::bindDouble(int index, double value)
{
    return stmt_ && sqlite3_bind_double(stmt_, index, value) == SQLITE_OK;
}

bool SqliteStatement::bindText(int index, const std::string& value)
{
    return stmt_ && sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

bool SqliteStatement::bindNull(int index)
{
    return stmt_ && sqlite3_bind_null(stmt_, index) == SQLITE_OK;
}

int SqliteStatement::step()
{
    if (!stmt_) return SQLITE_MISUSE;
    return sqlite3_step(stmt_);
}

bool SqliteStatement::reset()
{
    return stmt_ && sqlite3_reset(stmt_) == SQLITE_OK;
}

bool SqliteStatement::clearBindings()
{
    return stmt_ && sqlite3_clear_bindings(stmt_) == SQLITE_OK;
}

int SqliteStatement::columnInt(int col) const
{
    return stmt_ ? sqlite3_column_int(stmt_, col) : 0;
}

std::int64_t SqliteStatement::columnInt64(int col) const
{
    return stmt_ ? sqlite3_column_int64(stmt_, col) : 0;
}

double SqliteStatement::columnDouble(int col) const
{
    return stmt_ ? sqlite3_column_double(stmt_, col) : 0.0;
}

std::string SqliteStatement::columnText(int col) const
{
    if (!stmt_) return {};
    const unsigned char* text = sqlite3_column_text(stmt_, col);
    return text ? reinterpret_cast<const char*>(text) : std::string();
}
