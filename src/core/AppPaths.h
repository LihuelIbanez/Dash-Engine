#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AppPaths — runtime path resolution for resources and user data.
//
// Resolves correctly for:
//   • macOS .app bundle  (resources live in Contents/Resources/)
//   • Linux / development (falls back to PROJECT_DIR compile-time define)
// ─────────────────────────────────────────────────────────────────────────────
#include <string>
#include <filesystem>

namespace AppPaths {

// Returns the base directory that contains assets/, scenes/, library/, saves/.
// In a .app bundle: <bundle>/Contents/Resources
// In development:   PROJECT_DIR (compile-time macro)
inline std::string getResourcesDir()
{
    namespace fs = std::filesystem;

    // Locate the executable via /proc/self/exe (Linux) or argv[0] fallback.
    // On macOS we use the well-known bundle layout instead.
#if defined(__APPLE__)
    // __executable_path trick via dyld APIs is complex; use a simpler heuristic:
    // If we are inside a .app bundle, the executable is at:
    //   .app/Contents/MacOS/<exeName>
    // and resources are at:
    //   .app/Contents/Resources/
    //
    // We detect this by walking up from the current binary location.
    // SDL_GetBasePath() would also work but introduces an SDL dependency here.
    // Instead we use std::filesystem::canonical of argv[0] if available.
    // Since we cannot access argv here, we rely on a compile-time define as
    // fallback and trust that the project sets it correctly for dev builds.
    //
    // For .app bundles, packaging/install_app.sh copies resources into
    // Contents/Resources/ so the relative path "../Resources" from MacOS/ works.
    //
    // At runtime we attempt to detect via __FILE__ proximity; the reliable
    // cross-platform approach is the PROJECT_DIR macro for dev + relative path
    // detection for installed bundles via a sentinel file.
    {
        // Walk relative to the binary: try ../../Resources (bundle layout)
        // We check for the sentinel file assets/asset_db.json to confirm.
        // This path detection runs once (static local).
        static const std::string cached = []() -> std::string {
            // 1. Try bundle layout: look for assets/asset_db.json in
            //    well-known relative paths from the executable location.
            //    On macOS the executable is in Contents/MacOS/, so
            //    ../Resources is Contents/Resources/
            //    We approximate the executable dir via a known project sentinel.
            fs::path candidates[] = {
                fs::path("../Resources"),       // .app bundle at runtime
                fs::path("."),                  // same dir (e.g. test runner)
            };
            for (auto& rel : candidates) {
                std::error_code ec;
                fs::path abs = fs::canonical(rel, ec);
                if (!ec && fs::exists(abs / "assets" / "asset_db.json")) {
                    return abs.string();
                }
            }
            // 2. Fallback: compile-time development path
#ifdef PROJECT_DIR
            return std::string(PROJECT_DIR);
#else
            return ".";
#endif
        }();
        return cached;
    }
#elif defined(__linux__)
    static const std::string cached = []() -> std::string {
        std::error_code ec;
        fs::path self = fs::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            // binary is at <prefix>/bin/<name> → resources at <prefix>/share/DashEngine
            fs::path share = self.parent_path().parent_path() / "share" / "DashEngine";
            if (fs::exists(share / "assets" / "asset_db.json"))
                return share.string();
            // Portable fallback: sibling directory
            fs::path sibling = self.parent_path();
            if (fs::exists(sibling / "assets" / "asset_db.json"))
                return sibling.string();
        }
#ifdef PROJECT_DIR
        return std::string(PROJECT_DIR);
#else
        return ".";
#endif
    }();
    return cached;
#else
#  ifdef PROJECT_DIR
    return std::string(PROJECT_DIR);
#  else
    return ".";
#  endif
#endif
}

// Returns the user-writable directory for save files.
// macOS: ~/Library/Application Support/DashEngine/saves
// Linux: ~/.local/share/DashEngine/saves
// Fallback: ./saves
inline std::string getSavesDir()
{
    static const std::string cached = []() -> std::string {
        namespace fs = std::filesystem;
#if defined(__APPLE__)
        const char* home = std::getenv("HOME");
        if (home) {
            fs::path p = fs::path(home) / "Library" / "Application Support"
                         / "DashEngine" / "saves";
            std::error_code ec;
            fs::create_directories(p, ec);
            if (!ec) return p.string();
        }
#elif defined(__linux__)
        const char* xdg = std::getenv("XDG_DATA_HOME");
        if (xdg) {
            fs::path p = fs::path(xdg) / "DashEngine" / "saves";
            std::error_code ec;
            fs::create_directories(p, ec);
            if (!ec) return p.string();
        }
        const char* home = std::getenv("HOME");
        if (home) {
            fs::path p = fs::path(home) / ".local" / "share" / "DashEngine" / "saves";
            std::error_code ec;
            fs::create_directories(p, ec);
            if (!ec) return p.string();
        }
#endif
        return "saves";
    }();
    return cached;
}

// Returns the user-writable directory for editor configuration/preferences.
// macOS: ~/Library/Application Support/DashEngine
// Linux: ~/.config/DashEngine
// Fallback: ./config
inline std::string getConfigDir()
{
    static const std::string cached = []() -> std::string {
        namespace fs = std::filesystem;
#if defined(__APPLE__)
        const char* home = std::getenv("HOME");
        if (home) {
            fs::path p = fs::path(home) / "Library" / "Application Support" / "DashEngine";
            std::error_code ec;
            fs::create_directories(p, ec);
            if (!ec) return p.string();
        }
#elif defined(__linux__)
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg) {
            fs::path p = fs::path(xdg) / "DashEngine";
            std::error_code ec;
            fs::create_directories(p, ec);
            if (!ec) return p.string();
        }
        const char* home = std::getenv("HOME");
        if (home) {
            fs::path p = fs::path(home) / ".config" / "DashEngine";
            std::error_code ec;
            fs::create_directories(p, ec);
            if (!ec) return p.string();
        }
#endif
        return "config";
    }();
    return cached;
}

} // namespace AppPaths
