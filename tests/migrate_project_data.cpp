#include "project/ProjectDataMigrator.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: migrate_project_data <project_root> [project_name]\n";
        return 2;
    }

    ProjectManifest manifest;
    manifest.projectRoot = fs::absolute(argv[1]).string();
    manifest.name = (argc >= 3) ? argv[2] : fs::path(manifest.projectRoot).filename().string();

    auto result = ProjectDataMigrator::migrateJsonToSqlite(manifest);

    std::cout << "Migration result: " << (result.success ? "SUCCESS" : "FAILED") << "\n";
    std::cout << "DB: " << result.dbPath << "\n";
    std::cout << "Elapsed: " << result.summary.elapsedMs << " ms\n";
    std::cout << "Scenes: " << result.summary.scenes << "\n";
    std::cout << "Assets: " << result.summary.assets << "\n";
    std::cout << "PlayerClasses: " << result.summary.playerClasses << "\n";
    std::cout << "Enemies: " << result.summary.enemies << "\n";
    std::cout << "LootTables: " << result.summary.lootTables << "\n";
    std::cout << "Errors: " << result.summary.errorCount << "\n";

    for (const auto& line : result.log) {
        std::cout << line << "\n";
    }

    return result.success ? 0 : 1;
}
