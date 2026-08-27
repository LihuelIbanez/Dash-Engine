#pragma once
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// FileWatcher — polls a directory tree for file changes (Add/Modify/Delete).
// Call reset() once to establish baseline, then scan() each frame.
// ─────────────────────────────────────────────────────────────────────────────
class FileWatcher {
public:
    struct FileChange {
        std::string relativePath;
        enum Type { Added, Modified, Deleted } type;
    };

    FileWatcher() = default;
    explicit FileWatcher(const std::string& watchRoot, float pollIntervalSeconds = 1.0f);

    // Scan for changes. Throttled to pollIntervalSeconds.
    // Overwrites changes() with the newly detected set.
    void scan();

    // Return changes detected in the last scan().
    const std::vector<FileChange>& changes() const { return pendingChanges_; }

    // Rebuild snapshot from current disk state; clear pending changes.
    // Call once to establish baseline without generating spurious Added events.
    void reset();

private:
    std::string  watchRoot_;
    float        pollIntervalSeconds_ = 1.0f;
    std::chrono::steady_clock::time_point lastScan_{};  // epoch → scan always fires first
    std::unordered_map<std::string, std::string> stampSnapshot_;  // rel → "size:mtime"
    std::vector<FileChange> pendingChanges_;
};
