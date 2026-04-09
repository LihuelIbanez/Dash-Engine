#pragma once
#include "ProjectManifest.h"
#include "ProjectDataMigrator.h"
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// ProjectManager — manages the active project and the recent-projects list.
//
// Usage:
//   ProjectManager pm;
//   pm.loadRecents();
//   pm.openProject("/path/to/my-game.dashproject");
//   const auto& m = pm.manifest();     // paths are now absolute
//   pm.saveRecents();
// ─────────────────────────────────────────────────────────────────────────────
class ProjectManager {
public:
    struct MigrationStatus {
        bool attempted = false;
        bool success = false;
        std::string dbPath;
        std::vector<std::string> log;
        ProjectDataMigrator::Summary summary;
    };

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    // Create a new project directory at `dirPath` with the given name and
    // write the initial folder structure + .dashproject file.
    // On success the new project becomes the active one.
    bool createProject(const std::string& dirPath, const std::string& name);

    // Open an existing .dashproject file.
    // On success the project becomes the active one and the path is added to
    // the recent-projects list.
    bool openProject(const std::string& manifestPath);

    // Run JSON -> SQLite migration for the active project.
    // If force is false, migration only runs when DB does not exist.
    bool migrateProjectDataToSqlite(bool force = false);

    const MigrationStatus& lastMigrationStatus() const { return lastMigrationStatus_; }

    // Close the active project (clears state; does not delete files).
    void closeProject();

    // ── Query ─────────────────────────────────────────────────────────────────
    bool hasActiveProject() const { return active_; }
    const ProjectManifest& manifest() const { return manifest_; }

    // ── Recent projects ───────────────────────────────────────────────────────
    static constexpr int kMaxRecents = 10;

    const std::vector<std::string>& recentProjects() const { return recentPaths_; }

    // Add a path to the front of the recents list (deduplicates, max kMaxRecents).
    void addRecent(const std::string& path);

    // Persist recents to AppPaths::getConfigDir()/recents.json.
    void saveRecents() const;

    // Load recents from AppPaths::getConfigDir()/recents.json.
    // Entries that no longer exist on disk are silently dropped.
    void loadRecents();

private:
    ProjectManifest          manifest_;
    bool                     active_ = false;
    std::vector<std::string> recentPaths_;
    MigrationStatus          lastMigrationStatus_;
};
