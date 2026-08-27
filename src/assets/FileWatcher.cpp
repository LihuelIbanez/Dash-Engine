#include "FileWatcher.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace {
// Cheap change signal. Hashing file contents here made every poll read the whole
// assets tree (363 MB), which took longer than the poll interval and starved the
// editor's frame loop. ImportManager still content-hashes before reimporting.
std::string fileStamp(const fs::directory_entry& entry)
{
    std::error_code ec;
    auto size  = entry.file_size(ec);
    if (ec) return {};
    auto mtime = entry.last_write_time(ec);
    if (ec) return {};
    return std::to_string(static_cast<unsigned long long>(size)) + ':' +
           std::to_string(static_cast<long long>(mtime.time_since_epoch().count()));
}
} // namespace

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

    pendingChanges_.clear();
    if (!fs::is_directory(watchRoot_)) {
        lastScan_ = std::chrono::steady_clock::now();
        return;
    }

    // Build current-state snapshot
    std::unordered_map<std::string, std::string> current;
    for (auto& entry : fs::recursive_directory_iterator(
            watchRoot_, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file()) continue;
        std::string rel = fs::relative(entry.path(), watchRoot_).string();
        if (rel.empty() || rel[0] == '.' || rel == "asset_db.json") continue;
        current[rel] = fileStamp(entry);
    }

    // Detect Added and Modified
    for (auto& [rel, stamp] : current) {
        auto it = stampSnapshot_.find(rel);
        if (it == stampSnapshot_.end())
            pendingChanges_.push_back({rel, FileChange::Added});
        else if (it->second != stamp)
            pendingChanges_.push_back({rel, FileChange::Modified});
    }

    // Detect Deleted
    for (auto& [rel, stamp] : stampSnapshot_) {
        if (current.find(rel) == current.end())
            pendingChanges_.push_back({rel, FileChange::Deleted});
    }

    stampSnapshot_ = std::move(current);

    // Measure the interval between scans, not from scan start: a scan slower than
    // pollIntervalSeconds_ would otherwise re-fire on every frame.
    lastScan_ = std::chrono::steady_clock::now();
}

// ─────────────────────────────────────────────────────────────────────────────
void FileWatcher::reset()
{
    pendingChanges_.clear();
    stampSnapshot_.clear();
    if (!fs::is_directory(watchRoot_)) return;

    for (auto& entry : fs::recursive_directory_iterator(
            watchRoot_, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file()) continue;
        std::string rel = fs::relative(entry.path(), watchRoot_).string();
        if (rel.empty() || rel[0] == '.' || rel == "asset_db.json") continue;
        stampSnapshot_[rel] = fileStamp(entry);
    }

    // Reset timer so next scan() runs after pollIntervalSeconds_
    lastScan_ = std::chrono::steady_clock::now();
}
