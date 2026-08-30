#pragma once

#include "project/ProjectManifest.h"

#include <string>
#include <vector>

class ProjectDataMigrator {
public:
    struct Summary {
        int scenes = 0;
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

    struct SceneSyncSummary {
        int imported = 0;   // rows created or refreshed from a newer .json
        int upToDate = 0;   // rows whose .json has not changed since import
        int removed = 0;    // rows whose .json no longer exists on disk
        int errorCount = 0;
    };

    struct SceneSyncResult {
        bool success = false;
        std::string dbPath;
        std::vector<std::string> log;
        SceneSyncSummary summary;
    };

    static Result migrateJsonToSqlite(const ProjectManifest& manifest);

    // Refreshes the `scenes` cache table from the .json files on disk, which are
    // the source of truth (they are what git versions and the runtime loads).
    // A scene is re-imported when its file mtime is newer than the row's
    // `updated_at`, or when the row is missing.
    static SceneSyncResult syncScenesFromDisk(const ProjectManifest& manifest);
};
