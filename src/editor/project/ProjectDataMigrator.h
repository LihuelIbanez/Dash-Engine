#pragma once

#include "project/ProjectManifest.h"

#include <string>
#include <vector>

class ProjectDataMigrator {
public:
    struct Summary {
        int playerClasses = 0;
        int enemies = 0;
        int lootTables = 0;
        int lootEnemyLinks = 0;
        int lootDrops = 0;
        int assets = 0;
        int assetDependencies = 0;
        int errorCount = 0;
        double elapsedMs = 0.0;
    };

    struct Result {
        bool success = false;
        std::string dbPath;
        std::vector<std::string> log;
        Summary summary;
    };

    static Result migrateJsonToSqlite(const ProjectManifest& manifest);
};
