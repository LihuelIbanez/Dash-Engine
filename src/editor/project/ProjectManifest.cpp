#include "ProjectManifest.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ── Serialisation ─────────────────────────────────────────────────────────────

bool ProjectManifest::saveToFile(const std::string& path) const
{
    json j;
    j["formatVersion"]  = formatVersion;
    j["name"]           = name;
    j["defaultScene"]   = defaultScene;
    j["assetsDir"]      = assetsDir;
    j["scenesDir"]      = scenesDir;
    j["libraryDir"]     = libraryDir;
    j["buildOutputDir"] = buildOutputDir;
    j["gameConfig"]["screenWidth"]  = gameConfig.screenWidth;
    j["gameConfig"]["screenHeight"] = gameConfig.screenHeight;
    j["gameConfig"]["targetFps"]    = gameConfig.targetFps;

    std::ofstream ofs(path);
    if (!ofs.is_open()) return false;
    ofs << j.dump(4);
    return ofs.good();
}

bool ProjectManifest::loadFromFile(const std::string& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;

    json j;
    try { j = json::parse(ifs); }
    catch (...) { return false; }

    if (!j.contains("formatVersion")) return false;
    int ver = j["formatVersion"].get<int>();
    if (ver > kFormatVersion) return false;  // written by a newer editor version

    formatVersion  = ver;
    name           = j.value("name",           "Untitled");
    defaultScene   = j.value("defaultScene",   "scenes/default.json");
    assetsDir      = j.value("assetsDir",      "assets");
    scenesDir      = j.value("scenesDir",      "scenes");
    libraryDir     = j.value("libraryDir",     ".library");
    buildOutputDir = j.value("buildOutputDir", "build_output");

    if (j.contains("gameConfig")) {
        const auto& gc = j["gameConfig"];
        gameConfig.screenWidth  = gc.value("screenWidth",  1280);
        gameConfig.screenHeight = gc.value("screenHeight", 720);
        gameConfig.targetFps    = gc.value("targetFps",    60);
    }

    // Derive projectRoot from the file's parent directory.
    std::error_code ec;
    fs::path abs = fs::canonical(fs::path(path).parent_path(), ec);
    projectRoot = ec ? fs::path(path).parent_path().string() : abs.string();

    return true;
}

// ── Absolute path helpers ─────────────────────────────────────────────────────

static std::string joinProjectPath(const std::string& root, const std::string& rel)
{
    if (root.empty()) return rel;
    return (fs::path(root) / rel).string();
}

std::string ProjectManifest::absoluteAssetsDir() const
{
    return joinProjectPath(projectRoot, assetsDir);
}

std::string ProjectManifest::absoluteScenesDir() const
{
    return joinProjectPath(projectRoot, scenesDir);
}

std::string ProjectManifest::absoluteLibraryDir() const
{
    return joinProjectPath(projectRoot, libraryDir);
}

std::string ProjectManifest::absoluteBuildDir() const
{
    return joinProjectPath(projectRoot, buildOutputDir);
}

std::string ProjectManifest::absoluteDefaultScene() const
{
    return joinProjectPath(projectRoot, defaultScene);
}
