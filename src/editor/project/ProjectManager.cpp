#include "ProjectManager.h"
#include "AppPaths.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

static std::string recentsFilePath()
{
    return (fs::path(AppPaths::getConfigDir()) / "recents.json").string();
}

static std::string resolveManifestPath(const std::string& projectPath)
{
    std::error_code ec;
    fs::path inputPath = fs::path(projectPath);
    fs::path resolvedPath = fs::weakly_canonical(inputPath, ec);
    if (!ec) inputPath = resolvedPath;

    if (fs::is_regular_file(inputPath, ec)) {
        return inputPath.extension() == ".dashproject" ? inputPath.string() : std::string();
    }

    if (!fs::is_directory(inputPath, ec)) {
        return {};
    }

    std::vector<fs::path> manifests;
    for (const auto& entry : fs::directory_iterator(inputPath, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".dashproject") {
            manifests.push_back(entry.path());
        }
    }

    if (manifests.empty()) {
        return {};
    }

    if (manifests.size() == 1) {
        return manifests.front().string();
    }

    const std::string preferredName = inputPath.filename().string() + ".dashproject";
    for (const auto& manifest : manifests) {
        if (manifest.filename() == preferredName) {
            return manifest.string();
        }
    }

    return {};
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

bool ProjectManager::createProject(const std::string& dirPath, const std::string& name)
{
    std::error_code ec;

    // Create project root directory.
    fs::create_directories(dirPath, ec);
    if (ec) return false;

    // Set up manifest with user-supplied name.
    ProjectManifest m;
    m.name        = name.empty() ? "Untitled" : name;
    m.projectRoot = fs::canonical(dirPath, ec).string();
    if (ec) m.projectRoot = dirPath;

    // Create required sub-directories.
    for (const auto& sub : {m.assetsDir, m.scenesDir, m.libraryDir,
                             m.assetsDir + "/sprites", m.assetsDir + "/fonts",
                             m.assetsDir + "/data"}) {
        fs::create_directories(fs::path(m.projectRoot) / sub, ec);
    }

    // Write the .dashproject file.
    std::string manifestPath = (fs::path(m.projectRoot) / (m.name + ".dashproject")).string();
    if (!m.saveToFile(manifestPath)) return false;

    manifest_ = m;
    active_   = true;
    AppPaths::setActiveProjectPaths(
        manifest_.absoluteAssetsDir(),
        manifest_.absoluteScenesDir(),
        manifest_.absoluteLibraryDir(),
        manifest_.absoluteBuildDir());
    addRecent(manifestPath);
    return true;
}

bool ProjectManager::openProject(const std::string& manifestPath)
{
    const std::string resolvedManifestPath = resolveManifestPath(manifestPath);
    if (resolvedManifestPath.empty()) return false;

    ProjectManifest m;
    if (!m.loadFromFile(resolvedManifestPath)) return false;

    manifest_ = m;
    active_   = true;
    AppPaths::setActiveProjectPaths(
        manifest_.absoluteAssetsDir(),
        manifest_.absoluteScenesDir(),
        manifest_.absoluteLibraryDir(),
        manifest_.absoluteBuildDir());
    addRecent(resolvedManifestPath);
    return true;
}

void ProjectManager::closeProject()
{
    manifest_ = ProjectManifest{};
    active_   = false;
    AppPaths::clearActiveProjectPaths();
}

// ── Recent projects ───────────────────────────────────────────────────────────

void ProjectManager::addRecent(const std::string& path)
{
    // Normalise to absolute path string.
    std::error_code ec;
    std::string abs = path;
    fs::path p = fs::weakly_canonical(path, ec);
    if (!ec) abs = p.string();

    // Remove existing entry (dedup).
    recentPaths_.erase(
        std::remove(recentPaths_.begin(), recentPaths_.end(), abs),
        recentPaths_.end());

    // Insert at front.
    recentPaths_.insert(recentPaths_.begin(), abs);

    // Trim to max.
    if (static_cast<int>(recentPaths_.size()) > kMaxRecents)
        recentPaths_.resize(kMaxRecents);
}

void ProjectManager::saveRecents() const
{
    json j = json::array();
    for (const auto& p : recentPaths_)
        j.push_back(p);

    std::string fp = recentsFilePath();
    std::ofstream ofs(fp);
    if (ofs.is_open())
        ofs << j.dump(2);
}

void ProjectManager::loadRecents()
{
    std::string fp = recentsFilePath();
    std::ifstream ifs(fp);
    if (!ifs.is_open()) return;

    json j;
    try { j = json::parse(ifs); }
    catch (...) { return; }

    if (!j.is_array()) return;

    recentPaths_.clear();
    for (const auto& item : j) {
        if (!item.is_string()) continue;
        std::string path = item.get<std::string>();

        // Drop entries that no longer exist on disk.
        if (!fs::exists(path)) continue;

        recentPaths_.push_back(path);
        if (static_cast<int>(recentPaths_.size()) >= kMaxRecents) break;
    }
}
