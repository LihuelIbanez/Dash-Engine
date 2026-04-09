#pragma once
#include <string>
#include <filesystem>

// ─────────────────────────────────────────────────────────────────────────────
// ProjectManifest — describes a Dash-Engine project (.dashproject file).
//
// The .dashproject file is a JSON document that lives at the root of a project
// directory.  All paths stored in the manifest are relative to that root so
// the project folder is portable.
// ─────────────────────────────────────────────────────────────────────────────

struct GameConfig {
    int screenWidth  = 1280;
    int screenHeight = 720;
    int targetFps    = 60;
};

struct ProjectManifest {
    static constexpr int kFormatVersion = 1;

    int         formatVersion  = kFormatVersion;
    std::string name           = "Untitled";
    std::string defaultScene   = "scenes/default.json";
    std::string assetsDir      = "assets";
    std::string scenesDir      = "scenes";
    std::string libraryDir     = ".library";
    std::string buildOutputDir = "build_output";
    GameConfig  gameConfig;

    // Resolved at load time — the directory that contains the .dashproject file.
    // Not serialised.
    std::string projectRoot;

    // Serialise to / deserialise from the given file path.
    // On load, projectRoot is set to the parent directory of `path`.
    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);

    // ── Absolute path helpers ─────────────────────────────────────────────────
    std::string absoluteAssetsDir()    const;
    std::string absoluteScenesDir()    const;
    std::string absoluteLibraryDir()   const;
    std::string absoluteBuildDir()     const;
    std::string absoluteDefaultScene() const;
};
