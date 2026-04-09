#pragma once

#include <string>
#include <vector>

class SqliteDb;

class SchemaManager {
public:
    static constexpr int kCurrentSchemaVersion = 1;

    static bool applyMigrations(SqliteDb& db,
                                const std::string& migrationsDir,
                                std::vector<std::string>* log = nullptr);
};
