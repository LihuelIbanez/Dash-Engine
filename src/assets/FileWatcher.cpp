#include "FileWatcher.h"
#include "ImportManager.h"
#include <filesystem>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
FileWatcher::FileWatcher(const std::string& watchRoot, float pollIntervalSeconds)
    : watchRoot_(watchRoot)
    , pollIntervalSeconds_(pollIntervalSeconds)
    // Set lastScan_ to epoch so the very first call to scan() always fires.
    , lastScan_(std::chrono::steady_clock::time_point{})
{}

// ─────────────────────────────────────────────────────────────────────────────
void FileWatcher::scan()
{
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - lastScan_).count();
    if (elapsed < pollIntervalSeconds_) return;
    lastScan_ = now;

    pendingChanges_.clear();
    if (!fs::is_directory(watchRoot_)) return;

    // Build current-state snapshot
    std::unordered_map<std::string, std::string> current;
    for (auto& entry : fs::recursive_directory_iterator(
            watchRoot_, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file()) continue;
        std::string rel = fs::relative(entry.path(), watchRoot_).string();
        if (rel[0] == '.' || rel == "asset_db.json") continue;
        current[rel] = ImportManager::computeFileHash(entry.path().string());
    }

    // Detect Added and Modified
    for (auto& [rel, hash] : current) {
        auto it = hashSnapshot_.find(rel);
        if (it == hashSnapshot_.end())
            pendingChanges_.push_back({rel, FileChange::Added});
        else if (it->second != hash)
            pendingChanges_.push_back({rel, FileChange::Modified});
    }

    // Detect Deleted
    for (auto& [rel, hash] : hashSnapshot_) {
        if (current.find(rel) == current.end())
            pendingChanges_.push_back({rel, FileChange::Deleted});
    }

    hashSnapshot_ = std::move(current);
}

// ─────────────────────────────────────────────────────────────────────────────
void FileWatcher::reset()
{
    pendingChanges_.clear();
    hashSnapshot_.clear();
    if (!fs::is_directory(watchRoot_)) return;

    for (auto& entry : fs::recursive_directory_iterator(
            watchRoot_, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file()) continue;
        std::string rel = fs::relative(entry.path(), watchRoot_).string();
        if (rel[0] == '.' || rel == "asset_db.json") continue;
        hashSnapshot_[rel] = ImportManager::computeFileHash(entry.path().string());
    }

    // Reset timer so next scan() runs after pollIntervalSeconds_
    lastScan_ = std::chrono::steady_clock::now();
}
