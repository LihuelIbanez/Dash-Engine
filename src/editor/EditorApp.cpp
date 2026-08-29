#include "EditorApp.h"
#include "icon_data.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"
#include "imgui_internal.h"
#include "IsoRenderer.h"
#include "VersionInfo.h"
#include "PaintTileCommand.h"
#include "FloodFillCommand.h"
#include "HeightBrushCommand.h"
#include "PlaceEnemyCommand.h"
#include "EraseCommand.h"
#include "MoveEntityCommand.h"
#include "EditPropertyCommand.h"
#include "EditComponentFieldCommand.h"
#include "AddComponentCommand.h"
#include "RemoveComponentCommand.h"
#include "PlacePrefabCommand.h"
#include "PrefabAsset.h"
#include "Profiler.h"
#include "AppPaths.h"
#include "IconsFontAwesome6.h"
#include "TextureCache.h"
#include "db/DbMode.h"
#include "project/GameBuildPipeline.h"
#include "project/ProcessRunner.h"
#include "project/ProjectDataMigrator.h"
#include "EntityHierarchy.h"
#include "commands/CreateEntityCommand.h"
#include "rendering/vulkan/SceneLoader.h"
#include "rendering/vulkan/SceneRenderer.h"
#include "scene/SceneRepositorySqlite.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#  define NOMINMAX
#  include <windows.h>
#else
#  include <spawn.h>
#  include <sys/wait.h>
#  include <signal.h>
extern char** environ;
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

fs::path resolveBuiltGameExecutable(const fs::path& buildDir)
{
    std::error_code ec;
    const std::array<fs::path, 3> candidates = {
        buildDir / "IsometricRPG",
        buildDir / "src" / "game" / "IsometricRPG",
        buildDir / "Debug" / "IsometricRPG",
    };

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            return candidate;
        }
        ec.clear();
    }

    return {};
}

bool launchDetachedProcess(const fs::path& executable,
                           const std::vector<std::string>& args,
                           std::string& error)
{
#ifdef _WIN32
    std::string cmdLine = "\"" + executable.string() + "\"";
    for (const auto& arg : args) cmdLine += " \"" + arg + "\"";
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                        DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        error = "CreateProcess failed (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return true;
#else
    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const auto& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, executable.c_str(), nullptr, nullptr, argv.data(), environ);
    if (rc != 0) { error = std::strerror(rc); return false; }
    return true;
#endif
}

bool spawnTrackedProcess(const fs::path& executable,
                        const std::vector<std::string>& args,
                        intptr_t& outPid,
                        std::string& error)
{
#ifdef _WIN32
    std::string cmdLine = "\"" + executable.string() + "\"";
    for (const auto& arg : args) cmdLine += " \"" + arg + "\"";
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &si, &pi)) {
        error = "CreateProcess failed (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    outPid = reinterpret_cast<intptr_t>(pi.hProcess);
    return true;
#else
    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const auto& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, executable.c_str(), nullptr, nullptr, argv.data(), environ);
    if (rc != 0) { error = std::strerror(rc); return false; }
    outPid = static_cast<intptr_t>(pid);
    return true;
#endif
}

bool sqliteModeEnabled()
{
    return DbMode::usesSqliteRead(DbMode::current());
}

fs::path projectSqlitePath(const ProjectManifest& manifest)
{
    return fs::path(manifest.absoluteLibraryDir()) / "dash_engine.db";
}

std::string nowIso8601Local()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmLocal{};
#if defined(_WIN32)
    localtime_s(&tmLocal, &t);
#else
    localtime_r(&t, &tmLocal);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tmLocal, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

SDL_Color clampColor(SDL_Color c)
{
    auto clamp = [](int v) -> Uint8 {
        return static_cast<Uint8>(std::max(0, std::min(255, v)));
    };
    c.r = clamp(c.r);
    c.g = clamp(c.g);
    c.b = clamp(c.b);
    return c;
}

SDL_Color darken(SDL_Color c, int amount)
{
    c.r = static_cast<Uint8>(std::max(0, c.r - amount));
    c.g = static_cast<Uint8>(std::max(0, c.g - amount));
    c.b = static_cast<Uint8>(std::max(0, c.b - amount));
    return c;
}

void drawIsoDiamondScaled(SDL_Renderer* r, float cx, float cy, float hw, float hh, SDL_Color colTop)
{
    const SDL_Color colRight = darken(colTop, 18);
    const SDL_Color colLeft = darken(colTop, 12);
    SDL_Vertex verts[4] = {
        {{cx,      cy - hh}, colTop,   {0, 0}},
        {{cx + hw, cy     }, colRight, {0, 0}},
        {{cx,      cy + hh}, colTop,   {0, 0}},
        {{cx - hw, cy     }, colLeft,  {0, 0}},
    };
    const int idx[6] = {0, 1, 3, 1, 2, 3};
    SDL_RenderGeometry(r, nullptr, verts, 4, idx, 6);
}

void drawIsoColumn(SDL_Renderer* r, float cx, float cyBase, float hw, float hh, float hPx, SDL_Color topColor)
{
    const float cyTop = cyBase - hPx;
    const SDL_Color leftColor = darken(topColor, 45);
    const SDL_Color rightColor = darken(topColor, 60);

    SDL_Vertex leftFace[4] = {
        {{cx - hw, cyTop}, leftColor, {0, 0}},
        {{cx,      cyTop + hh}, leftColor, {0, 0}},
        {{cx,      cyBase + hh}, leftColor, {0, 0}},
        {{cx - hw, cyBase}, leftColor, {0, 0}},
    };
    SDL_Vertex rightFace[4] = {
        {{cx,      cyTop + hh}, rightColor, {0, 0}},
        {{cx + hw, cyTop}, rightColor, {0, 0}},
        {{cx + hw, cyBase}, rightColor, {0, 0}},
        {{cx,      cyBase + hh}, rightColor, {0, 0}},
    };
    const int idx[6] = {0, 1, 3, 1, 2, 3};
    SDL_RenderGeometry(r, nullptr, leftFace, 4, idx, 6);
    SDL_RenderGeometry(r, nullptr, rightFace, 4, idx, 6);
    drawIsoDiamondScaled(r, cx, cyTop, hw, hh, topColor);
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Initialisation
// ═════════════════════════════════════════════════════════════════════════════
bool EditorApp::init(const std::string& projectPath)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    window_ = SDL_CreateWindow(
        "Dash Engine",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1600, 900,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
    if (!window_) return false;

    // Set window icon from embedded BMP data
    SDL_RWops* rw = SDL_RWFromConstMem(kIconBmpData, static_cast<int>(kIconBmpLen));
    if (rw) {
        SDL_Surface* icon = SDL_LoadBMP_RW(rw, 1);
        if (icon) {
            // Key out the background colour (sample top-left corner pixel)
            Uint32 bgColor = *static_cast<Uint32*>(icon->pixels);
            SDL_SetColorKey(icon, SDL_TRUE, bgColor);
            SDL_SetWindowIcon(window_, icon);
            SDL_FreeSurface(icon);
        }
    }

    // ── ImGui setup ──────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // Load SF Pro (SFNS) on macOS; fall back to ImGui default on other platforms
#ifdef __APPLE__
    const char* sfProPath = "/System/Library/Fonts/SFNS.ttf";
    if (FILE* f = fopen(sfProPath, "rb")) {
        fclose(f);
        io.Fonts->AddFontFromFileTTF(sfProPath, 15.0f);
    } else {
        io.Fonts->AddFontDefault();
    }
#else
    io.Fonts->AddFontDefault();
#endif

    // Merge Font Awesome 6 solid icons into the same font atlas
    std::string faPath = AppPaths::getAssetsDir() + "/fonts/fa-solid-900.ttf";
    if (FILE* fa = fopen(faPath.c_str(), "rb")) {
        fclose(fa);
        ImFontConfig cfg;
        cfg.MergeMode        = true;
        cfg.GlyphMinAdvanceX = 13.f;   // keep icons monospace-ish
        cfg.PixelSnapH       = true;
        static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
        io.Fonts->AddFontFromFileTTF(faPath.c_str(), 13.f, &cfg, icon_ranges);
    }

    // VS Code Dark+ theme
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();

    // ── Style vars ───────────────────────────────────────────────────────────
    st.WindowRounding    = 0.f;
    st.FrameRounding     = 3.f;
    st.GrabRounding      = 3.f;
    st.TabRounding       = 0.f;
    st.ScrollbarRounding = 0.f;
    st.WindowBorderSize  = 1.f;
    st.FrameBorderSize   = 0.f;
    st.TabBorderSize     = 0.f;
    st.WindowPadding     = {8.f, 8.f};
    st.FramePadding      = {6.f, 4.f};
    st.ItemSpacing       = {8.f, 4.f};
    st.IndentSpacing     = 16.f;
    st.ScrollbarSize     = 12.f;
    st.GrabMinSize       = 8.f;

    // ── Colors ───────────────────────────────────────────────────────────────
    auto& c = st.Colors;

    // Backgrounds
    c[ImGuiCol_WindowBg]             = {0.118f, 0.118f, 0.118f, 1.f};  // #1E1E1E
    c[ImGuiCol_ChildBg]              = {0.118f, 0.118f, 0.118f, 1.f};
    c[ImGuiCol_PopupBg]              = {0.157f, 0.157f, 0.157f, 1.f};  // #282828
    c[ImGuiCol_MenuBarBg]            = {0.196f, 0.196f, 0.200f, 1.f};  // #323233

    // Borders
    c[ImGuiCol_Border]               = {0.278f, 0.278f, 0.278f, 1.f};  // #474747
    c[ImGuiCol_BorderShadow]         = {0.f, 0.f, 0.f, 0.f};

    // Title bars
    c[ImGuiCol_TitleBg]              = {0.118f, 0.118f, 0.118f, 1.f};  // #1E1E1E
    c[ImGuiCol_TitleBgActive]        = {0.196f, 0.196f, 0.200f, 1.f};  // #323233
    c[ImGuiCol_TitleBgCollapsed]     = {0.118f, 0.118f, 0.118f, 0.75f};

    // Tabs
    c[ImGuiCol_Tab]                  = {0.176f, 0.176f, 0.176f, 1.f};  // #2D2D2D
    c[ImGuiCol_TabHovered]           = {0.165f, 0.176f, 0.180f, 1.f};  // #2A2D2E
    c[ImGuiCol_TabSelected]          = {0.118f, 0.118f, 0.118f, 1.f};  // #1E1E1E (active=editor bg)
    c[ImGuiCol_TabDimmed]            = {0.145f, 0.145f, 0.149f, 1.f};  // #252526
    c[ImGuiCol_TabDimmedSelected]    = {0.176f, 0.176f, 0.176f, 1.f};

    // Headers (collapsing headers, tree nodes, selectables)
    c[ImGuiCol_Header]               = {0.149f, 0.310f, 0.471f, 0.5f}; // #264F78 selection
    c[ImGuiCol_HeaderHovered]        = {0.149f, 0.310f, 0.471f, 0.7f};
    c[ImGuiCol_HeaderActive]         = {0.149f, 0.310f, 0.471f, 0.9f};

    // Buttons
    c[ImGuiCol_Button]               = {0.235f, 0.235f, 0.235f, 1.f};  // #3C3C3C
    c[ImGuiCol_ButtonHovered]        = {0.310f, 0.310f, 0.310f, 1.f};  // #4F4F4F
    c[ImGuiCol_ButtonActive]         = {0.000f, 0.478f, 0.800f, 1.f};  // #007ACC

    // Frame backgrounds (inputs, sliders, checkboxes)
    c[ImGuiCol_FrameBg]              = {0.235f, 0.235f, 0.235f, 1.f};  // #3C3C3C
    c[ImGuiCol_FrameBgHovered]       = {0.278f, 0.278f, 0.278f, 1.f};  // #474747
    c[ImGuiCol_FrameBgActive]        = {0.200f, 0.200f, 0.200f, 1.f};  // #333333

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]          = {0.118f, 0.118f, 0.118f, 0.5f};
    c[ImGuiCol_ScrollbarGrab]        = {0.259f, 0.259f, 0.259f, 1.f};  // #424242
    c[ImGuiCol_ScrollbarGrabHovered] = {0.310f, 0.310f, 0.310f, 1.f};  // #4F4F4F
    c[ImGuiCol_ScrollbarGrabActive]  = {0.380f, 0.380f, 0.380f, 1.f};

    // Slider
    c[ImGuiCol_SliderGrab]           = {0.000f, 0.478f, 0.800f, 1.f};  // #007ACC
    c[ImGuiCol_SliderGrabActive]     = {0.067f, 0.467f, 0.733f, 1.f};  // #1177BB

    // Check mark
    c[ImGuiCol_CheckMark]            = {0.000f, 0.478f, 0.800f, 1.f};  // #007ACC

    // Text
    c[ImGuiCol_Text]                 = {0.800f, 0.800f, 0.800f, 1.f};  // #CCCCCC
    c[ImGuiCol_TextDisabled]         = {0.522f, 0.522f, 0.522f, 1.f};  // #858585

    // Separators
    c[ImGuiCol_Separator]            = {0.278f, 0.278f, 0.278f, 1.f};  // #474747
    c[ImGuiCol_SeparatorHovered]     = {0.000f, 0.478f, 0.800f, 0.7f};
    c[ImGuiCol_SeparatorActive]      = {0.000f, 0.478f, 0.800f, 1.f};

    // Resize grip
    c[ImGuiCol_ResizeGrip]           = {0.259f, 0.259f, 0.259f, 0.4f};
    c[ImGuiCol_ResizeGripHovered]    = {0.000f, 0.478f, 0.800f, 0.6f};
    c[ImGuiCol_ResizeGripActive]     = {0.000f, 0.478f, 0.800f, 0.9f};

    // Docking
    c[ImGuiCol_DockingPreview]       = {0.000f, 0.478f, 0.800f, 0.7f};
    c[ImGuiCol_DockingEmptyBg]       = {0.118f, 0.118f, 0.118f, 1.f};

    // Tables
    c[ImGuiCol_TableHeaderBg]        = {0.145f, 0.145f, 0.149f, 1.f};  // #252526
    c[ImGuiCol_TableBorderStrong]    = {0.278f, 0.278f, 0.278f, 1.f};
    c[ImGuiCol_TableBorderLight]     = {0.200f, 0.200f, 0.200f, 1.f};
    c[ImGuiCol_TableRowBg]           = {0.f, 0.f, 0.f, 0.f};
    c[ImGuiCol_TableRowBgAlt]        = {1.f, 1.f, 1.f, 0.03f};

    // Nav & misc
    c[ImGuiCol_NavHighlight]         = {0.000f, 0.478f, 0.800f, 1.f};
    c[ImGuiCol_TextSelectedBg]       = {0.149f, 0.310f, 0.471f, 0.5f};
    c[ImGuiCol_ModalWindowDimBg]     = {0.f, 0.f, 0.f, 0.55f};

    ImGui_ImplSDL2_InitForVulkan(window_);

    // ── Vulkan context (instance, device, swapchain, pipelines, ImGui backend) ─
    if (!vkCtx_.init(window_)) {
        std::fprintf(stderr, "EditorVkContext init failed.\n");
        return false;
    }

    // ── Scenes directory ─────────────────────────────────────────────────────
    // ── Scenes / asset paths ─────────────────────────────────────────────────
    projectManager_.loadRecents();
    if (!projectPath.empty()) {
        if (!openProject(projectPath)) {
            addLog("[WARN] Startup project could not be opened: " + projectPath);
        }
    }
    refreshProjectPaths();   // sets scenesDir_, assetsRoot_, libraryRoot_

    // ── File browser root ────────────────────────────────────────────────────
    fileEditorPanel_.init(AppPaths::getResourcesDir());

    // ── Asset Database ─────────────────────────────────────────────────────
    assetDbPath_ = assetsRoot_ + "/asset_db.json";
    if (fs::exists(assetDbPath_)) {
        if (assetDb_.load(assetDbPath_))
            addLog("Asset DB loaded (" + std::to_string(assetDb_.records().size()) + " records).");
        else
            addLog("[WARN] Failed to load asset DB.");
    } else {
        addLog("Asset DB not found, starting fresh.");
    }

    // ── Initial asset import ─────────────────────────────────────────────────
    {
        std::vector<std::string> importErrors;
        int count = importManager_.importAll(assetsRoot_, libraryRoot_, assetDb_, importErrors);
        if (count > 0)
            addLog("Imported " + std::to_string(count) + " asset(s).");
        for (auto& err : importErrors)
            addLog("[IMPORT] " + err);
        if (count > 0)
            assetDb_.save(assetDbPath_);
    }

    // ── File watcher baseline ────────────────────────────────────────────────
    fileWatcher_ = FileWatcher(assetsRoot_, 1.0f);
    fileWatcher_.reset(); // establish baseline (no spurious Added events)

    entityViewportPanel_.init(nullptr, assetsRoot_);

    spriteEditor_.init(nullptr);
    spriteEditor_.selectedEntityId = &selectedEntityId_;
    spriteEditor_.scene            = &scene_;
    spriteEditor_.commandStack     = &commandStack_;
    spriteEditor_.world            = &world_;
    spriteEditor_.importManager    = &importManager_;
    spriteEditor_.assetsRoot       = &assetsRoot_;
    spriteEditor_.libraryRoot      = &libraryRoot_;

    if (projectManager_.hasActiveProject()) {
        refreshSceneFiles();
        loadInitialProjectScene();
    } else {
        newScene();
    }
    syncUIRender3DSettingsFromScene();

    running_ = true;
    addLog("Editor ready (Vulkan viewport).");
    if (!projectManager_.hasActiveProject())
        addLog("No active project - Welcome panel opened.");
        // Panel is shown at startup only when no project was preloaded
        welcomePanel_.isOpen = !projectManager_.hasActiveProject();

    // ── Cursors ──────────────────────────────────────────────────────────────
    cursorArrow_     = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    cursorCrosshair_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    cursorHand_      = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    cursorMove_      = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);

    return true;
}

EditorApp::~EditorApp()
{
    if (playAuditActive_) {
        flushPlayAuditSessionToFile("editor_shutdown");
    }

    entityViewportPanel_.shutdown();

    // Persist asset database on shutdown
    if (!assetDbPath_.empty())
        assetDb_.save(assetDbPath_);

    SDL_FreeCursor(cursorArrow_);
    SDL_FreeCursor(cursorCrosshair_);
    SDL_FreeCursor(cursorHand_);
    SDL_FreeCursor(cursorMove_);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    vkCtx_.shutdown();
    if (window_)      SDL_DestroyWindow(window_);
    SDL_Quit();
}

void EditorApp::addLog(const std::string& msg)
{
    log_.push_back(msg);
    if (log_.size() > 500) log_.erase(log_.begin());

    if (playAuditActive_) {
        playAuditCurrentSessionLogs_.push_back(msg);
    }
}

std::string EditorApp::playAuditFilePath() const
{
    fs::path root;
    if (projectManager_.hasActiveProject() && !projectManager_.manifest().projectRoot.empty()) {
        root = projectManager_.manifest().projectRoot;
    } else {
        root = fs::path(BUILD_DIR).parent_path();
    }

    return (root / "audit" / "play_sessions_audit.json").string();
}

void EditorApp::beginPlayAuditSession()
{
    playAuditActive_ = true;
    playAuditSessionStartedAt_ = nowIso8601Local();
    playAuditCurrentSessionLogs_.clear();
    playAuditCurrentSessionLogs_.push_back("[AUDIT] Play session started at " + playAuditSessionStartedAt_);
}

void EditorApp::flushPlayAuditSessionToFile(const std::string& reason)
{
    if (!playAuditActive_) return;

    const std::string endedAt = nowIso8601Local();
    if (!reason.empty()) {
        playAuditCurrentSessionLogs_.push_back("[AUDIT] Session end reason: " + reason);
    }

    const std::string auditPath = playAuditFilePath();
    fs::create_directories(fs::path(auditPath).parent_path());

    json root;
    root["sessions"] = json::array();

    std::ifstream in(auditPath);
    if (in.is_open()) {
        try {
            in >> root;
        } catch (...) {
            root = json{};
            root["sessions"] = json::array();
        }
    }

    if (!root.contains("sessions") || !root["sessions"].is_array()) {
        root["sessions"] = json::array();
    }

    json session;
    session["startedAt"] = playAuditSessionStartedAt_;
    session["endedAt"] = endedAt;
    session["reason"] = reason;
    session["logs"] = playAuditCurrentSessionLogs_;

    root["sessions"].push_back(session);
    while (root["sessions"].size() > 2) {
        root["sessions"].erase(root["sessions"].begin());
    }

    std::ofstream out(auditPath);
    out << root.dump(2);

    playAuditActive_ = false;
    playAuditSessionStartedAt_.clear();
    playAuditCurrentSessionLogs_.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// Project management
// ═════════════════════════════════════════════════════════════════════════════

void EditorApp::refreshProjectPaths()
{
    if (projectManager_.hasActiveProject()) {
        const auto& m = projectManager_.manifest();
        assetsRoot_  = m.absoluteAssetsDir();
        libraryRoot_ = m.absoluteLibraryDir();
        scenesDir_   = m.absoluteScenesDir();
    } else {
        // Legacy mode: fall back to AppPaths-relative directories.
        assetsRoot_  = AppPaths::getAssetsDir();
        libraryRoot_ = AppPaths::getLibraryDir();
        scenesDir_   = AppPaths::getScenesDir();
    }
    fs::create_directories(scenesDir_);
}

void EditorApp::reinitAssetPipeline()
{
    // Save current DB before switching context.
    if (!assetDbPath_.empty())
        assetDb_.save(assetDbPath_);

    assetDbPath_ = assetsRoot_ + "/asset_db.json";
    assetDb_     = AssetDatabase{};
    if (fs::exists(assetDbPath_)) {
        if (assetDb_.load(assetDbPath_))
            addLog("Asset DB loaded (" + std::to_string(assetDb_.records().size()) + " records).");
        else
            addLog("[WARN] Failed to reload asset DB.");
    }

    // Re-import assets for the new root.
    std::vector<std::string> errors;
    int count = importManager_.importAll(assetsRoot_, libraryRoot_, assetDb_, errors);
    if (count > 0) {
        addLog("Imported " + std::to_string(count) + " asset(s).");
        assetDb_.save(assetDbPath_);
    }
    for (auto& e : errors) addLog("[IMPORT] " + e);

    // Reset file watcher to new assets directory.
    fileWatcher_ = FileWatcher(assetsRoot_, 1.0f);
    fileWatcher_.reset();

    // Refresh pointers held by the sprite editor.
    spriteEditor_.assetsRoot  = &assetsRoot_;
    spriteEditor_.libraryRoot = &libraryRoot_;
}

bool EditorApp::openProject(const std::string& manifestPath)
{
    addLog("[PROJ] openProject called with: " + manifestPath);
    if (manifestPath.empty()) {
        addLog("[PROJ] manifestPath is empty, skipping");
        return false;
    }
    
    std::fprintf(stdout, "[EditorApp::openProject] Starting migration and project load...\n");
    
    if (!projectManager_.openProject(manifestPath)) {
        addLog("[ERROR] Failed to open project: " + manifestPath);
        return false;
    }
    addLog("[PROJ] Project opened: " + projectManager_.manifest().name);
    addLog("[PROJ] Project root: " + projectManager_.manifest().projectRoot);
    addLog("[PROJ] Default scene: " + projectManager_.manifest().defaultScene);
    addLog("[PROJ] Absolute default scene: " + projectManager_.manifest().absoluteDefaultScene());
    
    // Log migration status
    const auto& migration = projectManager_.lastMigrationStatus();
    if (migration.attempted) {
        if (migration.success) {
            addLog("[PROJ:MIGRATION] SQLite migration succeeded: " + migration.dbPath);
        } else {
            addLog("[PROJ:MIGRATION] SQLite migration attempted but failed");
        }
    }
    
    refreshProjectPaths();

    const auto& migrationAgain = projectManager_.lastMigrationStatus();
    if (migrationAgain.attempted) {
        if (migrationAgain.success) {
            addLog("[MIGRATION] SQLite migration completed: " + migrationAgain.dbPath);
        } else {
            addLog("[MIGRATION] SQLite migration failed - using JSON fallback.");
        }
        for (const auto& line : migrationAgain.log)
            addLog("[MIGRATION] " + line);
    }

    refreshSceneFiles();
    addLog("[PROJ] Available scenes after refresh: " + std::to_string(sceneFiles_.size()));
    for (const auto& f : sceneFiles_) {
        addLog("[PROJ]   Scene: " + f);
    }
    
    loadInitialProjectScene();
    reinitAssetPipeline();
    projectManager_.saveRecents();
    return true;
}

bool EditorApp::createProject(const std::string& dirPath, const std::string& name)
{
    if (!projectManager_.createProject(dirPath, name)) {
        addLog("[ERROR] Failed to create project at: " + dirPath);
        return false;
    }
    addLog("Created project: " + name + "  (" + dirPath + ")");
    refreshProjectPaths();
    refreshSceneFiles();
    loadInitialProjectScene();
    reinitAssetPipeline();
    projectManager_.saveRecents();
    return true;
}

EntityData* EditorApp::findEntityById(uint64_t id)
{
    if (id == 0) return nullptr;
    for (auto& e : scene_.entities)
        if (e.id == id) return &e;
    return nullptr;
}

void EditorApp::performUndo()
{
    if (commandStack_.canUndo()) {
        commandStack_.undo(scene_, world_);
        addLog(std::string("Undo: ") + (commandStack_.redoName() ? commandStack_.redoName() : ""));
    }
}

void EditorApp::performRedo()
{
    if (commandStack_.canRedo()) {
        const char* n = commandStack_.redoName();
        commandStack_.redo(scene_, world_);
        addLog(std::string("Redo: ") + (n ? n : ""));
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Default Layout — Unity-style arrangement
//
//  ┌──────────┬─────────────────────────────┬────────────┐
//  │          │         Toolbar              │            │
//  │ Scene    ├─────────────────────────────-┤ Properties │
//  │ Hierarchy│                              │            │
//  │          │         Viewport             │            │
//  ├──────────┤         (centre)             │            │
//  │ Tile     │                              │            │
//  │ Palette  ├──────────────────────────────┤            │
//  │          │       Build Log              │            │
//  └──────────┴──────────────────────────────┴────────────┘
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::buildDefaultLayout(ImGuiID dockspaceId)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId,
        ImGuiDockNodeFlags_DockSpace);

    ImVec2 vpSize = ImGui::GetMainViewport()->WorkSize;
    ImGui::DockBuilderSetNodeSize(dockspaceId, vpSize);

    // Split: left panel (18%) | rest (82%)
    ImGuiID dockLeft, dockRemainder;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.18f,
                                &dockLeft, &dockRemainder);

    // Split remainder: centre+bottom | right panel (20%)
    ImGuiID dockRight, dockCentre;
    ImGui::DockBuilderSplitNode(dockRemainder, ImGuiDir_Right, 0.22f,
                                &dockRight, &dockCentre);

    // Split centre: top (toolbar) | middle+bottom
    ImGuiID dockToolbar, dockMiddle;
    ImGui::DockBuilderSplitNode(dockCentre, ImGuiDir_Up, 0.06f,
                                &dockToolbar, &dockMiddle);

    // Split middle: viewport (top ~75%) | build log (bottom ~25%)
    ImGuiID dockViewport, dockBottom;
    ImGui::DockBuilderSplitNode(dockMiddle, ImGuiDir_Down, 0.22f,
                                &dockBottom, &dockViewport);

    // Split left panel: scene hierarchy (top 55%) | tile palette (bottom 45%)
    ImGuiID dockHierarchy, dockPalette;
    ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Down, 0.45f,
                                &dockPalette, &dockHierarchy);

    // Split right panel: properties (top 55%) | file browser (bottom 45%)
    ImGuiID dockProperties, dockFileBrowser;
    ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.45f,
                                &dockFileBrowser, &dockProperties);

    // Dock each window into its slot
    ImGui::DockBuilderDockWindow("Toolbar",          dockToolbar);
    ImGui::DockBuilderDockWindow("Scene Hierarchy",  dockHierarchy);
    ImGui::DockBuilderDockWindow("Scene Selector",   dockHierarchy);
    ImGui::DockBuilderDockWindow("Tile Palette",     dockPalette);
    ImGui::DockBuilderDockWindow("Viewport",         dockViewport);
    ImGui::DockBuilderDockWindow("File Editor",      dockViewport);
    ImGui::DockBuilderDockWindow("Properties",       dockProperties);
    ImGui::DockBuilderDockWindow("File Browser",     dockFileBrowser);
    ImGui::DockBuilderDockWindow("Asset Browser",     dockBottom);
    ImGui::DockBuilderDockWindow("Asset Inspector",   dockProperties);
    ImGui::DockBuilderDockWindow("Build Log",        dockBottom);
    ImGui::DockBuilderDockWindow("Performance",      dockBottom);

    ImGui::DockBuilderFinish(dockspaceId);
}

// ═════════════════════════════════════════════════════════════════════════════
// Main loop
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::run()
{
    constexpr float TARGET_FRAME_MS = 1000.0f / 60.0f; // 60 fps cap

    // A runtime can be launched at any moment; leave a valid transport on disk.
    syncPlaybackStateFile(true);

    while (running_) {
        uint32_t frameStart = SDL_GetTicks();
        Profiler::instance().beginFrame();
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT)
                requestAction(PendingAction::Exit);

            // Global scene undo/redo shortcuts (Cmd+Z / Cmd+Shift+Z)
            if (ev.type == SDL_KEYDOWN && (ev.key.keysym.mod & KMOD_GUI)) {
                if (ev.key.keysym.sym == SDLK_z) {
                    if (ev.key.keysym.mod & KMOD_SHIFT)
                        performRedo();
                    else
                        performUndo();
                }
            }

            // Transport shortcuts, ignored while a text field owns the keyboard.
            if (ev.type == SDL_KEYDOWN && ev.key.repeat == 0 &&
                !ImGui::GetIO().WantTextInput) {
                handlePlaybackShortcut(ev.key.keysym.sym);
            }
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // ── Hot-reload: detect changed assets ────────────────────────────────
        fileWatcher_.scan();
        const auto& hwChanges = fileWatcher_.changes();
        if (!hwChanges.empty()) {
            if (editorMode_ == EditorMode::Edit && autoReload_) {
                std::vector<std::string> reloadErrors;
                bool dbChanged = importManager_.reimportChanged(
                    hwChanges, assetsRoot_, libraryRoot_, assetDb_, reloadErrors);
                for (const auto& ch : hwChanges)
                    addLog("[Hot-Reload] Reimported: " + ch.relativePath);
                for (const auto& err : reloadErrors)
                    addLog("[IMPORT] " + err);
                if (dbChanged) {
                    assetDb_.save(assetDbPath_);
                }
            } else if (editorMode_ == EditorMode::Play) {
                // Queue changes to apply when returning to Edit
                for (const auto& ch : hwChanges)
                    deferredReloads_.push_back(ch);
            }
        }

        // Full-window dockspace (reserve space for status bar)
        constexpr float kStatusBarHeight = 24.f;
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize({vp->WorkSize.x, vp->WorkSize.y - kStatusBarHeight});
        ImGui::SetNextWindowViewport(vp->ID);

        ImGuiWindowFlags dockFlags =
            ImGuiWindowFlags_NoDocking    | ImGuiWindowFlags_NoTitleBar  |
            ImGuiWindowFlags_NoCollapse   | ImGuiWindowFlags_NoResize    |
            ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus   | ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::Begin("##DockSpaceHost", nullptr, dockFlags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");

        // Build the default Unity-style layout on first frame
        if (!layoutInitialized_) {
            layoutInitialized_ = true;
            buildDefaultLayout(dockspaceId);
        }

        ImGui::DockSpace(dockspaceId, {0, 0});
        drawMenuBar();
            // Welcome panel must be drawn inside a window so OpenPopup is owned
            // by this context and can be reliably reopened from the File menu.
            welcomePanel_.draw(
                projectManager_.recentProjects(),
                [this](const std::string& p) { return openProject(p); },
                [this](const std::string& dir, const std::string& name) { return createProject(dir, name); },
                [this](const std::string& m) { addLog(m); }
            );
        ImGui::End();

        // Panels — always drawn so they can be docked
        if (showToolbar_) drawToolbar();
        if (showSceneHierarchy_) drawSceneHierarchy();
        if (showPropertiesPanel_) drawPropertiesPanel();
        if (showTilePalette_) drawTilePalette();
        if (showSceneSelector_) drawSceneSelector();
        if (showViewport_) drawViewport();
        if (showBuildLog_) drawBuildLog();
        if (showPerformancePanel_) drawPerformancePanel();
        if (showFileBrowser_)
            fileEditorPanel_.drawFileBrowser(AppPaths::getResourcesDir(), assetsRoot_,
                                            scenesDir_, [this](const std::string& m){ addLog(m); });
        if (showFileEditor_)
            fileEditorPanel_.drawFileEditor([this](const std::string& m){ addLog(m); });
        if (showAssetBrowser_)
            assetBrowserPanel_.draw(assetDb_, importManager_, assetsRoot_,
                                    libraryRoot_, assetDbPath_,
                                    [this](const std::string& m){ addLog(m); });
        if (showAssetInspector_)
            assetInspectorPanel_.draw(assetBrowserPanel_.selectedGuid(),
                                      assetDb_, importManager_, assetsRoot_,
                                      libraryRoot_, assetDbPath_,
                                      [this](const std::string& m){ addLog(m); });
        if (showValidationPanel_)
            validationPanel_.draw(validationIssues_, selectedEntityId_, camX_, camY_,
                [this]() {
                    validationIssues_ = contentValidator_.validate(scene_, world_, assetDb_);
                    addLog("Validation: " + std::to_string(validationIssues_.size()) + " issue(s) found.");
                });
        if (showRuntimeInspector_)
            runtimeInspectorPanel_.draw(scene_, editorMode_ == EditorMode::Play,
                                        selectedEntityId_, nullptr,
                                        [this](const std::string& m){ addLog(m); });
        if (showBoneStructurePanel_)
            boneStructurePanel_.draw(assetsRoot_, libraryRoot_,
                                     [this](const std::string& m){ addLog(m); });
        if (spriteEditor_.isOpen)
            spriteEditor_.draw();
        if (showAudioPanel_)
            audioPanel_.draw(assetDb_, scene_, world_, commandStack_, selectedEntityId_,
                             [this](const std::string& m){ addLog(m); });
        if (showLightingPanel_) drawLightingPanel();
        if (showEntityViewport_) {
            entityViewportPanel_.isOpen = true;
            entityViewportPanel_.draw(scene_, nullptr);
            if (!entityViewportPanel_.isOpen) showEntityViewport_ = false;
        }
        // ── About modal ────────────────────────────────────────────────────
        if (showAboutModal_) {
            ImGui::OpenPopup("About DashEngine");
            showAboutModal_ = false;
        }
        if (ImGui::BeginPopupModal("About DashEngine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("DashEngine v%s", DASH_VERSION_STRING);
            ImGui::Text("Commit: %s", DASH_GIT_COMMIT);
            ImGui::Text("Built:  %s", DASH_BUILD_DATE);
            ImGui::Separator();
            if (ImGui::Button("OK", {120, 0}))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (showOpenDialog_) drawOpenDialog();
        if (showSaveDialog_) drawSaveDialog();
        if (showCreateSceneDialog_) drawCreateSceneDialog();
        if (showConfirmDialog_) drawConfirmDialog();
        drawMigrationLogModal();
            // Update window title with dirty indicator and mode
        {
            std::string projectTitle = projectManager_.hasActiveProject()
                                     ? projectManager_.manifest().name
                                     : std::string("No Project");
            std::string title = "DashEngine - \"" + projectTitle + "\"";
            if (scene_.modified) title += " *";
            if (editorMode_ == EditorMode::Play) {
                title += playback_.paused() ? "  [PAUSED]" : "  [PLAYING]";
                title += "  " + playbackSpeedLabel();
            }
            SDL_SetWindowTitle(window_, title.c_str());
        }

        // ── Status Bar (VS Code style) ──────────────────────────────────────
        {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            float barY = vp->WorkPos.y + vp->WorkSize.y - kStatusBarHeight;
            float barW = vp->WorkSize.x;

            // Background color: blue in Edit, orange in Play
            ImU32 barColor = (editorMode_ == EditorMode::Edit)
                ? IM_COL32(0, 122, 204, 255)    // #007ACC
                : IM_COL32(204, 102, 51, 255);   // #CC6633
            dl->AddRectFilled({vp->WorkPos.x, barY},
                              {vp->WorkPos.x + barW, barY + kStatusBarHeight}, barColor);

            float textY = barY + 4.f;
            float x = vp->WorkPos.x + 10.f;

            // Mode indicator
            std::string modeText = "EDIT";
            if (editorMode_ == EditorMode::Play) {
                modeText = playback_.paused() ? "PAUSED" : "PLAYING";
                modeText += "  " + playbackSpeedLabel();
            }
            dl->AddText({x, textY}, IM_COL32(255, 255, 255, 255), modeText.c_str());
            x += ImGui::CalcTextSize(modeText.c_str()).x + 20.f;

            // Scene name
            if (!scene_.sceneName.empty()) {
                std::string sceneLabel = scene_.sceneName;
                if (scene_.modified) sceneLabel += " *";
                dl->AddText({x, textY}, IM_COL32(255, 255, 255, 220), sceneLabel.c_str());
                x += ImGui::CalcTextSize(sceneLabel.c_str()).x + 20.f;
            }

            // Entity count
            std::string entitiesStr = std::to_string(scene_.entities.size()) + " entities";
            dl->AddText({x, textY}, IM_COL32(255, 255, 255, 180), entitiesStr.c_str());

            // FPS (right-aligned)
            float fps = ImGui::GetIO().Framerate;
            char fpsStr[32];
            std::snprintf(fpsStr, sizeof(fpsStr), "%.0f FPS", fps);
            float fpsW = ImGui::CalcTextSize(fpsStr).x;
            dl->AddText({vp->WorkPos.x + barW - fpsW - 10.f, textY},
                        IM_COL32(255, 255, 255, 200), fpsStr);
        }

        // Push the transport to the runtime after the UI had a chance to change it.
        syncPlaybackStateFile();

        // Render
        ImGui::Render();
        if (vkCtx_.beginFrame()) {
            renderWorldToTexture();
            vkCtx_.endFrame();
        }
        Profiler::instance().endFrame();

        // Frame limiter — cap at 60fps to avoid spinning CPU/GPU
        uint32_t frameTime = SDL_GetTicks() - frameStart;
        if (frameTime < static_cast<uint32_t>(TARGET_FRAME_MS)) {
            SDL_Delay(static_cast<uint32_t>(TARGET_FRAME_MS) - frameTime);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Menu bar (inside the dockspace host window)
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawMenuBar()
{
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        // ── Project ──────────────────────────────────────────────────────────
        if (ImGui::MenuItem("New Project...", "Ctrl+Shift+N"))
            welcomePanel_.open();
        if (ImGui::MenuItem("Open Project...", "Ctrl+Shift+O"))
            welcomePanel_.open();
        ImGui::Separator();
        // ── Scene ─────────────────────────────────────────────────────────────
        if (ImGui::MenuItem(ICON_FA_FILE " New Scene",     "Ctrl+N"))
            requestAction(PendingAction::NewScene);
        if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Open Scene...", "Ctrl+O"))
            requestAction(PendingAction::OpenScene);
        if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK " Save",    "Ctrl+S")) {
            if (scene_.filePath.empty()) showSaveDialog_ = true;
            else saveScene(scene_.filePath);
        }
        if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK " Save As...")) showSaveDialog_ = true;
        ImGui::Separator();
        // ── Build ─────────────────────────────────────────────────────────────
        if (ImGui::MenuItem("Export Bundle",
                            nullptr, false,
                            projectManager_.hasActiveProject()))
            exportGameBundle();
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_DOOR_OPEN " Exit"))
            requestAction(PendingAction::Exit);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Toolbar", nullptr, &showToolbar_);
        ImGui::MenuItem("Scene Hierarchy", nullptr, &showSceneHierarchy_);
        ImGui::MenuItem("Properties", nullptr, &showPropertiesPanel_);
        ImGui::MenuItem("Tile Palette", nullptr, &showTilePalette_);
        ImGui::MenuItem("Viewport", nullptr, &showViewport_);
        ImGui::MenuItem("Build Log", nullptr, &showBuildLog_);
        ImGui::MenuItem("Performance", nullptr, &showPerformancePanel_);
        ImGui::MenuItem("File Browser", nullptr, &showFileBrowser_);
        ImGui::MenuItem("File Editor", nullptr, &showFileEditor_);
        ImGui::MenuItem("Asset Browser", nullptr, &showAssetBrowser_);
        ImGui::MenuItem("Asset Inspector", nullptr, &showAssetInspector_);
        ImGui::MenuItem("Lighting", nullptr, &showLightingPanel_);
        ImGui::MenuItem("Audio", nullptr, &showAudioPanel_);
        ImGui::MenuItem("Entity Viewport", nullptr, &showEntityViewport_);
        ImGui::MenuItem("Scene Selector", nullptr, &showSceneSelector_);
        ImGui::MenuItem("Validation Panel", nullptr, &showValidationPanel_);
        ImGui::MenuItem("Runtime Inspector", nullptr, &showRuntimeInspector_);
        ImGui::MenuItem("Bone Structure", nullptr, &showBoneStructurePanel_);
        ImGui::Separator();
        ImGui::MenuItem("Auto-Reload Assets", nullptr, &autoReload_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Assets")) {
        if (ImGui::MenuItem(ICON_FA_PAINTBRUSH " Sprite Editor")) spriteEditor_.isOpen = true;
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_ARROWS_ROTATE " Scan for Changes")) {
            // Force immediate scan regardless of poll interval
            fileWatcher_ = FileWatcher(assetsRoot_, 0.0f);
            fileWatcher_.scan();
            const auto& fwChanges = fileWatcher_.changes();
            if (fwChanges.empty()) {
                addLog("[Hot-Reload] No changes detected.");
            } else {
                std::vector<std::string> errs;
                bool dbChanged = importManager_.reimportChanged(
                    fwChanges, assetsRoot_, libraryRoot_, assetDb_, errs);
                for (const auto& ch : fwChanges)
                    addLog("[Hot-Reload] Reimported: " + ch.relativePath);
                for (const auto& err : errs)
                    addLog("[IMPORT] " + err);
                if (dbChanged) {
                    assetDb_.save(assetDbPath_);
                }
            }
            // Restore normal watcher
            fileWatcher_ = FileWatcher(assetsRoot_, 1.0f);
            fileWatcher_.reset();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        std::string undoLabel = ICON_FA_ROTATE_LEFT " Undo";
        if (commandStack_.canUndo())
            undoLabel += std::string(" (") + commandStack_.undoName() + ")";
        if (ImGui::MenuItem(undoLabel.c_str(), "Cmd+Z", false, commandStack_.canUndo()))
            performUndo();

        std::string redoLabel = ICON_FA_ROTATE_RIGHT " Redo";
        if (commandStack_.canRedo())
            redoLabel += std::string(" (") + commandStack_.redoName() + ")";
        if (ImGui::MenuItem(redoLabel.c_str(), "Cmd+Shift+Z", false, commandStack_.canRedo()))
            performRedo();

        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem(ICON_FA_SHIELD_HALVED " Validate Scene", "Shift+V")) {
            validationIssues_ = contentValidator_.validate(scene_, world_, assetDb_);
            showValidationPanel_ = true;
            addLog("Validation: " + std::to_string(validationIssues_.size()) + " issue(s) found.");
        }
        if (ImGui::MenuItem(ICON_FA_DATABASE " Migrate Project Data to SQLite",
                            nullptr,
                            false,
                            projectManager_.hasActiveProject())) {
            const bool ok = projectManager_.migrateProjectDataToSqlite(true);
            const auto& migration = projectManager_.lastMigrationStatus();
            migrationLastSuccess_ = migration.success;
            if (ok) {
                addLog("[MIGRATION] Manual migration completed: " + migration.dbPath);
            } else {
                addLog("[MIGRATION] Manual migration failed; JSON fallback remains active.");
            }

            std::ostringstream summary;
            summary << "Result: " << (migration.success ? "SUCCESS" : "FAILED") << "\n";
            summary << "Duration: " << migration.summary.elapsedMs << " ms\n";
            if (!migration.dbPath.empty())
                summary << "Database: " << migration.dbPath << "\n";
            summary << "Errors: " << migration.summary.errorCount << "\n";
            summary << "\nMigrated tables (rows):\n";
            summary << "scenes: " << migration.summary.scenes << "\n";
            summary << "assets: " << migration.summary.assets << "\n";
            summary << "asset_dependencies: " << migration.summary.assetDependencies << "\n";
            summary << "player_classes: " << migration.summary.playerClasses << "\n";
            summary << "enemies: " << migration.summary.enemies << "\n";
            summary << "loot_tables: " << migration.summary.lootTables << "\n";
            summary << "loot_table_enemies: " << migration.summary.lootEnemyLinks << "\n";
            summary << "loot_drops: " << migration.summary.lootDrops << "\n";
            migrationSummaryText_ = summary.str();

            std::ostringstream os;
            os << "Result: " << (migration.success ? "SUCCESS" : "FAILED") << "\n";
            if (!migration.dbPath.empty())
                os << "Database: " << migration.dbPath << "\n";
            os << "\nDetailed log:\n";
            for (const auto& line : migration.log)
                os << line << "\n";

            migrationLogText_ = os.str();
            showMigrationLogModal_ = true;

            refreshSceneFiles();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem(ICON_FA_CIRCLE_INFO " About DashEngine"))
            showAboutModal_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void EditorApp::drawMigrationLogModal()
{
    if (showMigrationLogModal_) {
        ImGui::OpenPopup("SQLite Migration Log");
        showMigrationLogModal_ = false;
    }

    if (ImGui::BeginPopupModal("SQLite Migration Log", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImVec4 statusColor = migrationLastSuccess_
            ? ImVec4(0.22f, 0.75f, 0.22f, 1.0f)
            : ImVec4(0.86f, 0.30f, 0.24f, 1.0f);
        ImGui::TextWrapped("Manual migration output.");
        ImGui::TextColored(statusColor, "%s", migrationLastSuccess_ ? "SUCCESS" : "FAILED");
        ImGui::Separator();

        std::vector<char> summaryBuffer(migrationSummaryText_.begin(), migrationSummaryText_.end());
        summaryBuffer.push_back('\0');
        ImGui::InputTextMultiline(
            "##migration_summary",
            summaryBuffer.data(),
            summaryBuffer.size(),
            ImVec2(760.0f, 180.0f),
            ImGuiInputTextFlags_ReadOnly);

        ImGui::Separator();
        ImGui::TextWrapped("Detailed log (select and copy):");

        ImVec2 size(760.0f, 340.0f);
        std::vector<char> logBuffer(migrationLogText_.begin(), migrationLogText_.end());
        logBuffer.push_back('\0');
        ImGui::InputTextMultiline(
            "##migration_log",
            logBuffer.data(),
            logBuffer.size(),
            size,
            ImGuiInputTextFlags_ReadOnly);

        if (ImGui::Button("Close", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawToolbar()
{
    ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoScrollbar);

    // ▶ Build & Run (green)
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.220f, 0.541f, 0.204f, 1.f}); // #388A34
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  {0.298f, 0.686f, 0.314f, 1.f}); // #4CAF50
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   {0.180f, 0.490f, 0.196f, 1.f});
    if (ImGui::Button(ICON_FA_HAMMER "  Build & Run  ", {170, 34})) buildAndRun();
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // ▶ Play / ■ Stop (in-editor play mode)
    if (editorMode_ == EditorMode::Edit) {
        ImGui::PushStyleColor(ImGuiCol_Button,       {0.000f, 0.478f, 0.800f, 1.f}); // #007ACC
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.067f, 0.467f, 0.733f, 1.f}); // #1177BB
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.055f, 0.388f, 0.612f, 1.f}); // #0E639C
        if (ImGui::Button(ICON_FA_PLAY "  Play  ", {110, 34})) enterPlayMode();
        ImGui::PopStyleColor(3);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,       {0.957f, 0.278f, 0.278f, 1.f}); // #F44747
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.827f, 0.184f, 0.184f, 1.f}); // #D32F2F
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.710f, 0.150f, 0.150f, 1.f});
        if (ImGui::Button(ICON_FA_STOP "  Stop  ", {110, 34})) exitPlayMode();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        drawPlaybackControls();
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Tool buttons (disabled in Play mode)
    bool inEdit = (editorMode_ == EditorMode::Edit);
    auto toolBtn = [&](const char* label, Tool t) {
        bool sel = (currentTool_ == t);
        if (!inEdit) ImGui::BeginDisabled();
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, {0.035f, 0.278f, 0.443f, 1.f}); // #094771
        if (ImGui::Button(label, {110, 34})) currentTool_ = t;
        if (sel) ImGui::PopStyleColor();
        if (!inEdit) ImGui::EndDisabled();
        ImGui::SameLine();
    };

    toolBtn(ICON_FA_ARROW_POINTER " Select",      Tool::Select);
    toolBtn(ICON_FA_SKULL         " Place Enemy",  Tool::PlaceEnemy);
    toolBtn(ICON_FA_ERASER        " Erase",        Tool::Erase);

    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // ✔ Validate Scene
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.773f, 0.525f, 0.753f, 1.f}); // #C586C0
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  {0.808f, 0.576f, 0.847f, 1.f}); // #CE93D8
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   {0.690f, 0.440f, 0.680f, 1.f});
    if (ImGui::Button(ICON_FA_SHIELD_HALVED "  Validate  ", {130, 34})) {
        validationIssues_ = contentValidator_.validate(scene_, world_, assetDb_);
        showValidationPanel_ = true;
        addLog("Validation: " + std::to_string(validationIssues_.size()) + " issue(s) found.");
    }
    ImGui::PopStyleColor(3);

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════════
// Play-mode transport – pause / step / time scale
// ═════════════════════════════════════════════════════════════════════════════
namespace {
constexpr float       kPlaybackSpeeds[]      = { 0.25f, 0.5f, 1.0f, 2.0f };
constexpr const char* kPlaybackSpeedLabels[] = { "0.25x", "0.5x", "1x", "2x" };
constexpr int         kPlaybackSpeedCount    = 4;
} // namespace

void EditorApp::drawPlaybackControls()
{
    const bool paused = playback_.paused();

    if (ImGui::Button(paused ? ICON_FA_PLAY "  Resume  " : ICON_FA_PAUSE "  Pause  ", {130, 34}))
        playback_.togglePause();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s the running game (F6)", paused ? "Resume" : "Pause");

    ImGui::SameLine();
    ImGui::BeginDisabled(!paused);
    if (ImGui::Button(ICON_FA_FORWARD_STEP "  Step  ", {110, 34}))
        playback_.requestStep();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Advance a single frame (F10)");

    ImGui::SameLine();
    int speedIndex = 2;
    for (int i = 0; i < kPlaybackSpeedCount; ++i) {
        if (std::fabs(playback_.timeScale() - kPlaybackSpeeds[i]) < 0.001f) {
            speedIndex = i;
            break;
        }
    }
    ImGui::SetNextItemWidth(90);
    if (ImGui::Combo("##playbackSpeed", &speedIndex, kPlaybackSpeedLabels, kPlaybackSpeedCount))
        playback_.setTimeScale(kPlaybackSpeeds[speedIndex]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Simulation speed");
}

std::string EditorApp::playbackSpeedLabel() const
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%gx", static_cast<double>(playback_.timeScale()));
    return buf;
}

void EditorApp::handlePlaybackShortcut(SDL_Keycode key)
{
    switch (key) {
    case SDLK_F5:
        if (editorMode_ == EditorMode::Edit) enterPlayMode();
        else                                 exitPlayMode();
        break;
    case SDLK_F6:
        if (editorMode_ == EditorMode::Play) {
            playback_.togglePause();
            addLog(playback_.paused() ? "[Play] Paused." : "[Play] Resumed.");
        }
        break;
    case SDLK_F10:
        // Stepping only makes sense while paused, so pause first if needed.
        if (editorMode_ == EditorMode::Play) {
            playback_.setPaused(true);
            playback_.requestStep();
        }
        break;
    default:
        break;
    }
}

std::string EditorApp::playbackStatePath() const
{
    return std::string(BUILD_DIR) + "/generated/vulkan_viewport_state.json";
}

void EditorApp::syncPlaybackStateFile(bool force)
{
    const bool dirty = playback_.consumeDirty();
    if (!dirty && !force) return;

    const fs::path path = fs::path(playbackStatePath());
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    // Merge instead of overwrite: other blocks (camera, viewport…) may live here.
    json state = json::object();
    {
        std::ifstream in(path);
        if (in.is_open()) {
            try { in >> state; } catch (...) { state = json::object(); }
        }
    }
    if (!state.is_object()) state = json::object();

    state["playback"] = {
        {"paused",     playback_.paused()},
        {"timeScale",  playback_.timeScale()},
        {"stepSerial", playback_.stepSerial()},
    };

    // Write-then-rename so the polling runtime never reads a half-written file.
    const fs::path tmp = fs::path(path).concat(".tmp");
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) return;
        out << state.dump(2);
    }
    fs::rename(tmp, path, ec);
    if (ec) fs::remove(tmp, ec);
}

// ═════════════════════════════════════════════════════════════════════════════
// Scene Hierarchy
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawSceneHierarchy()
{
    ImGui::Begin("Scene Hierarchy");

    ImGui::Text("Scene: %s%s", scene_.sceneName.c_str(),
                scene_.modified ? " *" : "");
    ImGui::Separator();

    for (uint64_t rootId : dash::editor::rootEntities(scene_))
        drawHierarchyNode(rootId, 0);

    // Dropping on the empty area below the tree unparents the entity.
    ImGui::Dummy({-1, 18});
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("DASH_ENTITY")) {
            uint64_t dragged = 0;
            std::memcpy(&dragged, p->Data, sizeof(dragged));
            reparentEntity(dragged, 0);
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::Separator();
    if (editorMode_ == EditorMode::Play) ImGui::BeginDisabled();
    if (ImGui::Button("+ Add Enemy", {-1, 0})) {
        uint64_t newId = scene_.allocateEntityId();
        auto cmd = std::make_unique<PlaceEnemyCommand>(camX_, camY_, newId, "NewEnemy");
        commandStack_.execute(std::move(cmd), scene_, world_);
        setSelection(newId);
        addLog("Entity added.");
    }
    if (ImGui::Button("+ Add Light", {-1, 0})) {
        EntityData light;
        light.id = scene_.allocateEntityId();
        light.type = EntityData::Type::Enemy;  // scene entity types are Player/Enemy only
        light.name = "Light";
        light.x = camX_;
        light.y = camY_;
        TransformComponent tf;
        tf.x = camX_; tf.y = camY_; tf.z = 2.0f;
        light.components.push_back(tf);
        light.components.push_back(LightComponent{});
        const uint64_t newId = light.id;
        commandStack_.execute(
            std::make_unique<CreateEntityCommand>(std::move(light), "Create Light"),
            scene_, world_);
        setSelection(newId);
        addLog("Light added.");
    }

    EntityData* sel = findEntityById(selectedEntityId_);
    if (sel && sel->type != EntityData::Type::Player) {
        if (ImGui::Button("- Remove Selected", {-1, 0})) {
            auto cmd = std::make_unique<EraseCommand>(selectedEntityId_);
            commandStack_.execute(std::move(cmd), scene_, world_);
            clearSelection();
        }
    }
    if (editorMode_ == EditorMode::Play) ImGui::EndDisabled();

    ImGui::End();
}

void EditorApp::drawSceneSelector()
{
    ImGui::Begin("Scene Selector");

    if (!projectManager_.hasActiveProject()) {
        ImGui::TextDisabled("Open a project to browse scenes.");
        ImGui::End();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.000f, 0.478f, 0.800f, 1.f)); // #007ACC
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.067f, 0.467f, 0.733f, 1.f)); // #1177BB
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.055f, 0.388f, 0.612f, 1.f));
    if (ImGui::Button(ICON_FA_ARROWS_ROTATE " Refresh", {120, 0})) {
        refreshSceneFiles();
    }
    ImGui::PopStyleColor(3);

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.220f, 0.541f, 0.204f, 1.f)); // #388A34
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.298f, 0.686f, 0.314f, 1.f)); // #4CAF50
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.180f, 0.490f, 0.196f, 1.f));
    if (ImGui::Button(ICON_FA_FILE " Create", {120, 0})) {
        std::strncpy(createSceneFileName_, "new_scene.json", sizeof(createSceneFileName_));
        createSceneFileName_[sizeof(createSceneFileName_) - 1] = '\0';
        showCreateSceneDialog_ = true;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.808f, 0.569f, 0.471f, 1.f)); // #CE9178
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.878f, 0.639f, 0.541f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.710f, 0.490f, 0.400f, 1.f));
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " Open", {120, 0})) {
        if (!selectedSceneFile_.empty()) {
            openScene(selectedSceneFile_);
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::Separator();
    ImGui::TextDisabled(ICON_FA_FILE " Project scenes:");

    if (ImGui::BeginListBox("##scene_selector_list", ImVec2(-FLT_MIN, -FLT_MIN))) {
        if (sceneFiles_.empty()) {
            ImGui::Selectable("(No scene files in scenes/)", false, ImGuiSelectableFlags_Disabled);
        } else {
            for (const auto& sceneFile : sceneFiles_) {
                const bool isSelected = (sceneFile == selectedSceneFile_);
                std::string label = std::string(ICON_FA_FILE " ") + sceneFile + "##" + sceneFile;
                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    selectedSceneFile_ = sceneFile;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        openScene(sceneFile);
                    }
                }
            }
        }
        ImGui::EndListBox();
    }

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════════
// Properties Panel
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawPropertiesPanel()
{
    ImGui::Begin("Properties");

    // ── World settings (always visible) ──────────────────────────────────────
    if (ImGui::CollapsingHeader("World Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        int seed = static_cast<int>(scene_.worldSeed);
        if (ImGui::InputInt("Seed", &seed)) {
            scene_.worldSeed = static_cast<unsigned int>(seed);
            world_.generate(scene_.worldSeed);
            applySceneToWorld();
            scene_.modified = true;
        }

        char nameBuf[128];
        std::strncpy(nameBuf, scene_.sceneName.c_str(), sizeof(nameBuf));
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (ImGui::InputText("Scene Name", nameBuf, sizeof(nameBuf))) {
            scene_.sceneName = nameBuf;
            scene_.modified  = true;
        }

        ImGui::Separator();
        ImGui::TextDisabled("3D Isometric (Vulkan)");
        bool changed3D = false;

        auto applyCameraPreset = [&](float yaw, float pitch, float distance, float height, float zoom) {
            viewport3D_.isoYawDeg = yaw;
            viewport3D_.isoPitchDeg = pitch;
            viewport3D_.cameraDistance = distance;
            viewport3D_.cameraHeight = height;
            viewport3D_.zoom = zoom;
            changed3D = true;
        };

        ImGui::TextDisabled("Camera Presets");
        if (ImGui::Button("Diablo", ImVec2(90, 0))) {
            applyCameraPreset(45.0f, 35.264f, 10.0f, 2.5f, 1.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("RTS", ImVec2(90, 0))) {
            applyCameraPreset(45.0f, 42.0f, 16.0f, 4.0f, 0.85f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Close Follow", ImVec2(110, 0))) {
            applyCameraPreset(45.0f, 28.0f, 6.5f, 1.8f, 1.2f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset", ImVec2(80, 0))) {
            applyCameraPreset(45.0f, 35.264f, 8.0f, 2.5f, 1.0f);
        }

        changed3D |= ImGui::SliderFloat("Iso Yaw", &viewport3D_.isoYawDeg, 30.0f, 60.0f, "%.1f deg");
        changed3D |= ImGui::SliderFloat("Iso Pitch", &viewport3D_.isoPitchDeg, 20.0f, 45.0f, "%.1f deg");
        changed3D |= ImGui::SliderFloat("Camera Distance", &viewport3D_.cameraDistance, 4.0f, 24.0f, "%.2f");
        changed3D |= ImGui::SliderFloat("Camera Height", &viewport3D_.cameraHeight, 0.0f, 12.0f, "%.2f");
        changed3D |= ImGui::SliderFloat("Viewport Zoom", &viewport3D_.zoom, 0.5f, 2.5f, "%.2f");
        changed3D |= ImGui::SliderFloat("Height Scale", &viewport3D_.heightScale, 12.0f, 72.0f, "%.1f px");
        changed3D |= ImGui::SliderFloat("Grid Opacity", &viewport3D_.gridOpacity, 0.0f, 0.8f, "%.2f");
        if (changed3D) {
            syncSceneRender3DSettingsFromUI();
            scene_.modified = true;
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Vulkan Viewport: Active");
    }

    ImGui::Separator();

    EntityData* ep = findEntityById(selectedEntityId_);
    if (!ep) {
        ImGui::TextDisabled("Select an entity to edit.");
        ImGui::End();
        return;
    }

    if (selection_.size() > 1) {
        ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f),
                           "%d entities selected - edits apply to all",
                           static_cast<int>(selection_.size()));
        ImGui::TextDisabled("Showing: %s (active)", ep->name.c_str());
        ImGui::Separator();
    }

    auto& e = *ep;

    // ── Entity header (EntityData-level fields) ───────────────────────────────
    if (ImGui::CollapsingHeader("Entity", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Prefab badge
        if (!e.prefabGuid.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.75f, 0.2f, 1.f));
            ImGui::Text("Prefab instance: %s", e.prefabGuid.c_str());
            ImGui::PopStyleColor();
            if (ImGui::Button("Reset All to Prefab Defaults")) {
                std::string prefabsDir = assetsRoot_ + "/prefabs";
                PrefabAsset prefab = findPrefabByGuid(prefabsDir, e.prefabGuid);
                if (!prefab.guid.empty()) {
                    e.components           = instantiate(prefab);
                    e.componentOverrides   = nlohmann::json::object();
                    scene_.modified        = true;
                }
            }
            ImGui::Separator();
        }

        // Name
        char nameBuf[128];
        std::strncpy(nameBuf, e.name.c_str(), sizeof(nameBuf));
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        static std::string nameSnap;
        ImGui::InputText("Name", nameBuf, sizeof(nameBuf));
        if (ImGui::IsItemActivated())            nameSnap = e.name;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            std::string nv(nameBuf);
            if (nv != nameSnap)
                commandStack_.execute(std::make_unique<EditPropertyCommand>(
                    e.id, PropertyTarget::Name,
                    PropertyValue{nameSnap}, PropertyValue{nv}), scene_, world_);
        }

        ImGui::Text("Type: %s",
            e.type == EntityData::Type::Player ? "Player" : "Enemy");

        if (e.type == EntityData::Type::Player) {
            const char* classes[] = {"Warrior", "Mage", "Rogue", "Archer"};
            int cur = 0;
            for (int i = 0; i < 4; ++i)
                if (e.charClass == classes[i]) { cur = i; break; }
            if (ImGui::Combo("Class", &cur, classes, 4)) {
                std::string oldClass = e.charClass;
                std::string newClass = classes[cur];
                e.charClass = oldClass;
                commandStack_.execute(std::make_unique<EditPropertyCommand>(
                    e.id, PropertyTarget::CharClass,
                    PropertyValue{oldClass}, PropertyValue{newClass}), scene_, world_);
            }
        }
    }

    // ── Generic component inspector ───────────────────────────────────────────
    // Snapshot statics (only one field can be active at a time in ImGui)
    static PropertyValue fieldSnap;
    static bool          hasFieldSnap  = false;
    static char          strBuf[256]   = {};
    static std::string   strSnap;

    // Track which component type to remove (deferred to avoid iterator invalidation)
    ComponentType pendingRemove    = ComponentType::Transform;
    bool          hasPendingRemove = false;

    // Available sprite names from assets/sprites/*.png (without extension).
    std::vector<std::string> availableSprites;
    availableSprites.push_back("default");
    {
        std::error_code ec;
        fs::path spritesDir = fs::path(assetsRoot_) / "sprites";
        if (fs::exists(spritesDir, ec) && fs::is_directory(spritesDir, ec)) {
            for (const auto& entry : fs::directory_iterator(spritesDir, ec)) {
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() != ".png") continue;
                availableSprites.push_back(entry.path().stem().string());
            }
        }
    }
    std::sort(availableSprites.begin(), availableSprites.end());
    availableSprites.erase(std::unique(availableSprites.begin(), availableSprites.end()),
                           availableSprites.end());

    for (std::size_t ci = 0; ci < e.components.size(); ++ci) {
        auto& comp = e.components[ci];
        ComponentType ct = getVariantType(comp);
        const ComponentMeta& meta = getComponentMeta(ct);

        ImGui::PushID(static_cast<int>(ci));

        // Header with small remove button at the right edge
        bool sectionOpen = ImGui::CollapsingHeader(
            meta.name.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        float btnW = ImGui::GetFrameHeight();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - btnW);
        if (ImGui::SmallButton("x")) {
            pendingRemove    = ct;
            hasPendingRemove = true;
        }

        if (sectionOpen) {
            for (const auto& prop : meta.properties) {
                void* ptr = fieldPtr(comp, prop);
                ImGui::PushID(prop.name.c_str());

                switch (prop.type) {
                case PropertyType::Float: {
                    float* fptr = static_cast<float*>(ptr);
                    ImGui::DragFloat(prop.name.c_str(), fptr, 0.05f);
                    if (ImGui::IsItemActivated()) {
                        fieldSnap    = *fptr;
                        hasFieldSnap = true;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit() && hasFieldSnap) {
                        float nv = *fptr;
                        if (nv != std::get<float>(fieldSnap)) {
                            *fptr = std::get<float>(fieldSnap);
                            applyComponentFieldEdit(e.id, ct, prop,
                                                    fieldSnap, PropertyValue{nv});
                        }
                        hasFieldSnap = false;
                    }
                    break;
                }
                case PropertyType::Int: {
                    int* iptr = static_cast<int*>(ptr);
                    ImGui::DragInt(prop.name.c_str(), iptr);
                    if (ImGui::IsItemActivated()) {
                        fieldSnap    = *iptr;
                        hasFieldSnap = true;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit() && hasFieldSnap) {
                        int nv = *iptr;
                        if (nv != std::get<int>(fieldSnap)) {
                            *iptr = std::get<int>(fieldSnap);
                            applyComponentFieldEdit(e.id, ct, prop,
                                                    fieldSnap, PropertyValue{nv});
                        }
                        hasFieldSnap = false;
                    }
                    break;
                }
                case PropertyType::String: {
                    std::string* sptr = static_cast<std::string*>(ptr);
                    bool handledWithSpritePicker = false;

                    // Render.sprite: pick from discovered sprites for faster workflows.
                    if (ct == ComponentType::Render && prop.name == "sprite") {
                        handledWithSpritePicker = true;

                        std::vector<std::string> pickerItems = availableSprites;
                        if (std::find(pickerItems.begin(), pickerItems.end(), *sptr) == pickerItems.end())
                            pickerItems.insert(pickerItems.begin(), *sptr);

                        std::string comboLabel = prop.name + "##picker";
                        if (ImGui::BeginCombo(comboLabel.c_str(), sptr->c_str())) {
                            for (const auto& item : pickerItems) {
                                bool selected = (*sptr == item);
                                if (ImGui::Selectable(item.c_str(), selected) && item != *sptr) {
                                    applyComponentFieldEdit(e.id, ct, prop,
                                                            PropertyValue{*sptr}, PropertyValue{item});
                                }
                                if (selected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        std::string manualLabel = "Manual##" + prop.name;
                        std::strncpy(strBuf, sptr->c_str(), 255);
                        strBuf[255] = '\0';
                        ImGui::InputText(manualLabel.c_str(), strBuf, sizeof(strBuf));
                        if (ImGui::IsItemActivated())
                            strSnap = *sptr;
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            std::string nv(strBuf);
                            if (nv != strSnap)
                                applyComponentFieldEdit(e.id, ct, prop,
                                                        PropertyValue{strSnap}, PropertyValue{nv});
                        }
                    }

                    if (!handledWithSpritePicker) {
                        std::strncpy(strBuf, sptr->c_str(), 255);
                        strBuf[255] = '\0';
                        ImGui::InputText(prop.name.c_str(), strBuf, sizeof(strBuf));
                        if (ImGui::IsItemActivated())
                            strSnap = *sptr;
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            std::string nv(strBuf);
                            if (nv != strSnap)
                                applyComponentFieldEdit(e.id, ct, prop,
                                                        PropertyValue{strSnap}, PropertyValue{nv});
                        }
                    }
                    break;
                }
                case PropertyType::Bool: {
                    bool* bptr  = static_cast<bool*>(ptr);
                    bool  prev  = *bptr;
                    if (ImGui::Checkbox(prop.name.c_str(), bptr) && *bptr != prev) {
                        bool nv = *bptr;
                        *bptr   = prev;
                        applyComponentFieldEdit(e.id, ct, prop,
                                                PropertyValue{prev}, PropertyValue{nv});
                    }
                    break;
                }
                case PropertyType::Enum: {
                    int* iptr = static_cast<int*>(ptr);
                    int  prev = *iptr;
                    std::vector<const char*> items;
                    for (const auto& s : prop.enumValues) items.push_back(s.c_str());
                    if (ImGui::Combo(prop.name.c_str(), iptr,
                                     items.data(), static_cast<int>(items.size()))) {
                        int nv = *iptr;
                        *iptr  = prev;
                        applyComponentFieldEdit(e.id, ct, prop,
                                                PropertyValue{prev}, PropertyValue{nv});
                    }
                    break;
                }
                } // switch

                ImGui::PopID();
            } // for props
        } // if sectionOpen

        ImGui::PopID();

        if (hasPendingRemove) break; // stop iterating; will remove after loop
    }

    // Apply deferred component removal
    if (hasPendingRemove) {
        for (auto& comp : e.components) {
            if (getVariantType(comp) == pendingRemove) {
                commandStack_.execute(
                    std::make_unique<RemoveComponentCommand>(e.id, comp),
                    scene_, world_);
                break;
            }
        }
    }

    // Recompute overrides for prefab instances after any change
    if (!e.prefabGuid.empty()) {
        std::string prefabsDir = assetsRoot_ + "/prefabs";
        PrefabAsset prefab = findPrefabByGuid(prefabsDir, e.prefabGuid);
        if (!prefab.guid.empty())
            e.componentOverrides = computeOverrides(prefab, e.components);
    }

    // ── Add Component button ──────────────────────────────────────────────────
    ImGui::Separator();

    // Collect component types not yet present on this entity
    static int addSel = 0;
    std::vector<ComponentType> missing;
    for (int i = 0; i <= static_cast<int>(ComponentType::Physics); ++i) {
        ComponentType ct = static_cast<ComponentType>(i);
        bool found = false;
        for (const auto& comp : e.components)
            if (getVariantType(comp) == ct) { found = true; break; }
        if (!found) missing.push_back(ct);
    }

    if (!missing.empty()) {
        if (addSel >= static_cast<int>(missing.size())) addSel = 0;
        std::vector<const char*> names;
        for (auto ct : missing) names.push_back(getComponentMeta(ct).name.c_str());
        ImGui::SetNextItemWidth(180.f);
        ImGui::Combo("##addcomp", &addSel, names.data(), static_cast<int>(names.size()));
        ImGui::SameLine();
        if (ImGui::Button("+ Add")) {
            ComponentType toAdd = missing[addSel];
            ComponentVariant newComp;
            switch (toAdd) {
            case ComponentType::Transform: newComp = TransformComponent{}; break;
            case ComponentType::Render:    newComp = RenderComponent{};    break;
            case ComponentType::Health:    newComp = HealthComponent{};    break;
            case ComponentType::Mana:      newComp = ManaComponent{};      break;
            case ComponentType::Stats:     newComp = StatsComponent{};     break;
            case ComponentType::Combat:    newComp = CombatComponent{};    break;
            case ComponentType::AI:        newComp = AIComponent{};        break;
            case ComponentType::Physics:   newComp = PhysicsComponent{};   break;
            }
            commandStack_.execute(
                std::make_unique<AddComponentCommand>(e.id, newComp),
                scene_, world_);
        }
    } else {
        ImGui::TextDisabled("All components present.");
    }

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════════
// Tile Palette
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawTilePalette()
{
    ImGui::Begin("Tile Palette");

    // ── Map tool buttons (icon-only, 28×28) ─────────────────────────────────
    {
        auto mapToolBtn = [&](const char* icon, Tool t, const char* tip) {
            bool active = (currentTool_ == t);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(icon, {28, 28})) currentTool_ = t;
            if (active) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            ImGui::SameLine();
        };

        mapToolBtn(ICON_FA_PAINTBRUSH,  Tool::PaintTile,    "Paint Tile");
        mapToolBtn(ICON_FA_FILL_DRIP,   Tool::FillTile,    "Flood Fill");
        mapToolBtn(ICON_FA_EYE_DROPPER, Tool::EyeDropper,  "Eyedropper (Pick Tile)");
        mapToolBtn(ICON_FA_MOUNTAIN,    Tool::HeightBrush,  "Height Brush (Sculpt)");
        mapToolBtn(ICON_FA_STAIRS,      Tool::CliffBrush,   "Cliff Brush (WC3)");
        mapToolBtn(ICON_FA_PALETTE,     Tool::TexturePaint, "Texture Paint");
        mapToolBtn(ICON_FA_WATER,       Tool::WaterTool,    "Water Tool");
        mapToolBtn(ICON_FA_ERASER,      Tool::Erase,        "Erase (Reset to Grass)");
        ImGui::NewLine();
    }

    // ── Brush size slider (visible for PaintTile) ───────────────────────────
    if (currentTool_ == Tool::PaintTile) {
        ImGui::Text(ICON_FA_BRUSH " Size");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##brush", &brushSize_, 1, 5);
    }

    // ── Height brush settings ───────────────────────────────────────────────
    if (currentTool_ == Tool::HeightBrush) {
        const char* modeNames[] = {"Raise", "Lower", "Smooth", "Flatten"};
        int modeIdx = static_cast<int>(heightBrushMode_);
        ImGui::Text("Mode");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::Combo("##hbmode", &modeIdx, modeNames, 4))
            heightBrushMode_ = static_cast<HeightBrushMode>(modeIdx);

        ImGui::Text("Radius");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##hbradius", &heightBrushRadius_, 1, 8);

        ImGui::Text("Strength");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderFloat("##hbstr", &heightBrushStrength_, 0.01f, 0.20f, "%.3f");
    }

    // ── Cliff brush settings ────────────────────────────────────────────────
    if (currentTool_ == Tool::CliffBrush) {
        const char* cliffModes[] = {"Raise", "Lower"};
        int cmi = (cliffBrushMode_ == CliffBrushCommand::Mode::Raise) ? 0 : 1;
        ImGui::Text("Mode");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::Combo("##cbmode", &cmi, cliffModes, 2))
            cliffBrushMode_ = (cmi == 0) ? CliffBrushCommand::Mode::Raise
                                         : CliffBrushCommand::Mode::Lower;

        ImGui::Text("Radius");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##cbradius", &cliffBrushRadius_, 1, 5);
    }

    // ── Texture paint settings ──────────────────────────────────────────────
    if (currentTool_ == Tool::TexturePaint) {
        struct TexInfo { const char* name; TerrainTextureId id; ImVec4 col; };
        TexInfo textures[] = {
            {"Grass",     TerrainTextureId::Grass,     {0.24f, 0.47f, 0.16f, 1.f}},
            {"Dirt",      TerrainTextureId::Dirt,       {0.43f, 0.29f, 0.16f, 1.f}},
            {"Rock",      TerrainTextureId::Rock,       {0.47f, 0.45f, 0.41f, 1.f}},
            {"Sand",      TerrainTextureId::Sand,       {0.71f, 0.63f, 0.35f, 1.f}},
            {"Snow",      TerrainTextureId::Snow,       {0.86f, 0.88f, 0.92f, 1.f}},
            {"Mud",       TerrainTextureId::Mud,        {0.35f, 0.22f, 0.10f, 1.f}},
            {"Dark Grass",TerrainTextureId::DarkGrass,  {0.12f, 0.29f, 0.10f, 1.f}},
            {"Gravel",    TerrainTextureId::Gravel,     {0.55f, 0.53f, 0.49f, 1.f}},
            {"Ice",       TerrainTextureId::Ice,        {0.72f, 0.85f, 0.93f, 1.f}},
        };
        ImGui::Text("Texture:");
        const float texBtnSz = 24.f;
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        float aw = ImGui::GetContentRegionAvail().x;
        int tcols = std::max(1, (int)((aw + sp) / (texBtnSz + sp)));
        int ti = 0;
        for (auto& tx : textures) {
            bool sel = (selectedTexture_ == tx.id);
            ImGui::PushID(100 + ti);
            ImGui::PushStyleColor(ImGuiCol_Button, tx.col);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                {tx.col.x + 0.12f, tx.col.y + 0.12f, tx.col.z + 0.12f, 1.f});
            if (sel) {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
                ImGui::PushStyleColor(ImGuiCol_Border, {1.f, 1.f, 0.f, 1.f});
            }
            if (ImGui::Button("##tex", {texBtnSz, texBtnSz}))
                selectedTexture_ = tx.id;
            if (sel) {
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tx.name);
            ++ti;
            if (ti % tcols != 0) ImGui::SameLine();
            ImGui::PopID();
        }
        if (ti % tcols != 0) ImGui::NewLine();

        ImGui::Text("Strength");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderFloat("##tpstr", &texturePaintStrength_, 0.1f, 1.0f, "%.2f");

        ImGui::Text("Radius");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##tpradius", &texturePaintRadius_, 1, 8);
    }

    // ── Water tool settings ─────────────────────────────────────────────────
    if (currentTool_ == Tool::WaterTool) {
        ImGui::Text("Water Level");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderFloat("##wlevel", &waterLevel_, 0.0f, 8.0f, "%.1f");

        ImGui::Text("Body ID");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##wbody", &selectedWaterBodyId_, 1, 8);

        if (ImGui::Button("Set Water Level")) {
            auto& bodies = world_.terrain().waterBodies();
            for (auto& wb : bodies) {
                if (wb.id == static_cast<uint8_t>(selectedWaterBodyId_)) {
                    float oldLevel = wb.waterLevel;
                    if (oldLevel != waterLevel_) {
                        auto cmd = std::make_unique<WaterLevelCommand>(
                            wb.id, oldLevel, waterLevel_);
                        commandStack_.execute(std::move(cmd), scene_, world_);
                    }
                    break;
                }
            }
        }
    }

    ImGui::Separator();

    // ── Tile grid (compact color buttons) ───────────────────────────────────
    struct TileInfo { const char* name; TileType type; ImVec4 col; };
    TileInfo tiles[] = {
        {"Deep Water", TileType::DeepWater, {0.06f, 0.16f, 0.39f, 1.f}},
        {"Water",      TileType::Water,     {0.12f, 0.27f, 0.55f, 1.f}},
        {"Sand",       TileType::Sand,      {0.71f, 0.63f, 0.35f, 1.f}},
        {"Grass",      TileType::Grass,     {0.24f, 0.47f, 0.16f, 1.f}},
        {"Forest",     TileType::Forest,    {0.12f, 0.29f, 0.10f, 1.f}},
        {"Dirt",       TileType::Dirt,      {0.43f, 0.29f, 0.16f, 1.f}},
        {"Stone",      TileType::Stone,     {0.47f, 0.45f, 0.41f, 1.f}},
        {"Mountain",   TileType::Mountain,  {0.37f, 0.33f, 0.31f, 1.f}},
        {"Snow",       TileType::Snow,      {0.86f, 0.88f, 0.92f, 1.f}},
    };

    const float btnSize = 28.f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    float availW = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)((availW + spacing) / (btnSize + spacing)));

    const char* selectedName = "None";
    int idx = 0;
    for (auto& t : tiles) {
        bool sel = (selectedTileType_ == t.type);
        if (sel) selectedName = t.name;

        ImGui::PushID(idx);
        ImGui::PushStyleColor(ImGuiCol_Button, t.col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            {t.col.x + 0.12f, t.col.y + 0.12f, t.col.z + 0.12f, 1.f});

        if (sel) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
            ImGui::PushStyleColor(ImGuiCol_Border, {1.f, 1.f, 0.f, 1.f});
        }

        if (ImGui::Button("##tile", {btnSize, btnSize})) {
            selectedTileType_ = t.type;
            if (currentTool_ != Tool::PaintTile &&
                currentTool_ != Tool::FillTile)
                currentTool_ = Tool::PaintTile;
        }

        if (sel) {
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
        ImGui::PopStyleColor(2);

        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.name);

        ++idx;
        if (idx % cols != 0)
            ImGui::SameLine();
        ImGui::PopID();
    }

    // ── Selected tile info ──────────────────────────────────────────────────
    ImGui::Separator();
    ImGui::TextDisabled("Selected: %s", selectedName);

    // ── Terrain Rendering settings ─────────────────────────────────────────
    if (ImGui::CollapsingHeader("Terrain Rendering")) {
        ImGui::Text("Height Scale");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderFloat("##hscale", &viewport3D_.heightScale, 12.0f, 72.0f, "%.0f");

        ImGui::Text("Grid Opacity");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderFloat("##gopacity", &viewport3D_.gridOpacity, 0.0f, 1.0f, "%.2f");

        ImGui::Checkbox("Distance Fog", &viewport3D_.fogEnabled);
        if (viewport3D_.fogEnabled) {
            ImGui::Text("Fog Start");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::SliderFloat("##fogstart", &viewport3D_.fogStart, 10.0f, 100.0f, "%.0f");

            ImGui::Text("Fog End");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::SliderFloat("##fogend", &viewport3D_.fogEnd, 20.0f, 200.0f, "%.0f");
        }
    }

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════════
// Viewport
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawViewport()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("Viewport");

    // Get viewport coordinates and size BEFORE rendering
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1) avail.x = 1;
    if (avail.y < 1) avail.y = 1;
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();

    // Render the world to the offscreen Vulkan texture (done in main loop after beginFrame)

    // Use full available area (Vulkan viewport auto-resizes)
    ImVec2 imgSize = avail;

    // Fill background with dark color
    ImDrawList* bgDl = ImGui::GetWindowDrawList();
    bgDl->AddRectFilled(cursorPos, {cursorPos.x + avail.x, cursorPos.y + avail.y}, IM_COL32(30, 30, 30, 255));

    // Display the Vulkan-rendered viewport texture
    ImGui::Image(vkCtx_.viewportTexture(), imgSize);

    // Update viewport mapping for mouse coordinate conversion
    vpDisplayW_ = imgSize.x;
    vpDisplayH_ = imgSize.y;
    vpScreenX_ = cursorPos.x;
    vpScreenY_ = cursorPos.y;

    // ── Prefab drag-drop target ──────────────────────────────────────────────
    if (editorMode_ == EditorMode::Edit && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PREFAB_GUID")) {
            std::string guid(static_cast<const char*>(payload->Data),
                             static_cast<size_t>(payload->DataSize) - 1);
            ImGuiIO& io = ImGui::GetIO();
            float mx = io.MousePos.x - vpScreenX_;
            float my = io.MousePos.y - vpScreenY_;
            float wx = 0.f, wy = 0.f;
            if (viewportScreenToWorld(mx, my, wx, wy)) {
                std::string prefabsDir = assetsRoot_ + "/prefabs";
                PrefabAsset prefab = findPrefabByGuid(prefabsDir, guid);
                if (!prefab.guid.empty()) {
                    uint64_t newId = scene_.allocateEntityId();
                    auto comps = instantiate(prefab);
                    auto cmd = std::make_unique<PlacePrefabCommand>(
                        wx, wy, newId, prefab.name, guid, std::move(comps));
                    commandStack_.execute(std::move(cmd), scene_, world_);
                    setSelection(newId);
                    addLog("Placed prefab: " + prefab.name);
                } else {
                    addLog("ERROR: Prefab not found for GUID: " + guid);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // ── Interaction ──────────────────────────────────────────────────────────
    bool vpFocused = ImGui::IsWindowFocused();
    bool vpHovered = ImGui::IsItemHovered();

    // ── Transform gizmo ──────────────────────────────────────────────────────
    // Runs before the tools below so a gizmo drag swallows the click instead of
    // painting a tile or moving the entity underneath.
    bool gizmoOwnsPointer = false;
    float gizmoViewProj[16];
    buildViewProjMatrix(vpDisplayW_, vpDisplayH_, gizmoViewProj);
    const dash::gizmo::ViewportRect gizmoRect{vpScreenX_, vpScreenY_, vpDisplayW_, vpDisplayH_};
    if (editorMode_ == EditorMode::Edit) {
        handleGizmoShortcuts(vpFocused);

        ImGuiIO& gio = ImGui::GetIO();
        gizmoOwnsPointer = updateViewportGizmo(gizmoViewProj, gizmoRect,
                                               gio.MousePos.x, gio.MousePos.y, vpHovered);

        drawSelectionOverlays(ImGui::GetWindowDrawList(), gizmoViewProj, gizmoRect);
        if (gizmoOwnsPointer) vpHovered = false;
    }

    // ── Play-mode input: Vulkan handles its own input ────────────────────────
    // (clicking in viewport sends input to Vulkan process, not editor)

    // ── Edit-mode interaction ────────────────────────────────────────────────

    // Edit mode: WASD pans camera
    if (vpFocused && editorMode_ == EditorMode::Edit) {
        ImGuiIO& io = ImGui::GetIO();
        float speed = 12.f * io.DeltaTime;
        if (ImGui::IsKeyDown(ImGuiKey_W)) { camX_ -= speed; camY_ -= speed; }
        if (ImGui::IsKeyDown(ImGuiKey_S)) { camX_ += speed; camY_ += speed; }
        if (ImGui::IsKeyDown(ImGuiKey_A)) { camX_ -= speed; camY_ += speed; }
        if (ImGui::IsKeyDown(ImGuiKey_D)) { camX_ += speed; camY_ -= speed; }
    }

    // Play mode: WASD moves player entity, camera follows
    if (vpFocused && editorMode_ == EditorMode::Play) {
        ImGuiIO& io = ImGui::GetIO();
        float speed = 6.f * io.DeltaTime;
        float dx = 0.f, dy = 0.f;
        if (ImGui::IsKeyDown(ImGuiKey_W)) { dx -= 1.f; dy -= 1.f; }
        if (ImGui::IsKeyDown(ImGuiKey_S)) { dx += 1.f; dy += 1.f; }
        if (ImGui::IsKeyDown(ImGuiKey_A)) { dx -= 1.f; dy += 1.f; }
        if (ImGui::IsKeyDown(ImGuiKey_D)) { dx += 1.f; dy -= 1.f; }

        if (dx != 0.f || dy != 0.f) {
            float len = std::sqrt(dx * dx + dy * dy);
            dx = dx / len * speed;
            dy = dy / len * speed;
            for (auto& e : scene_.entities) {
                if (e.type == EntityData::Type::Player) {
                    e.x += dx;
                    e.y += dy;
                    camX_ = e.x;
                    camY_ = e.y;
                    break;
                }
            }
        }
    }

    if (vpHovered) {
        // Change cursor based on active tool
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            SDL_SetCursor(cursorMove_);
        else if (currentTool_ == Tool::PaintTile)
            SDL_SetCursor(cursorCrosshair_);
        else if (currentTool_ == Tool::FillTile)
            SDL_SetCursor(cursorCrosshair_);
        else if (currentTool_ == Tool::EyeDropper)
            SDL_SetCursor(cursorCrosshair_);
        else if (currentTool_ == Tool::HeightBrush)
            SDL_SetCursor(cursorCrosshair_);
        else if (currentTool_ == Tool::PlaceEnemy)
            SDL_SetCursor(cursorCrosshair_);
        else if (currentTool_ == Tool::Erase)
            SDL_SetCursor(cursorCrosshair_);
        else
            SDL_SetCursor(cursorHand_);

        ImGuiIO& io = ImGui::GetIO();

        // Scroll → zoom camera (change orbit distance)
        if (io.MouseWheel != 0.f) {
            viewport3D_.cameraDistance -= io.MouseWheel * 1.5f;
            viewport3D_.cameraDistance = std::clamp(viewport3D_.cameraDistance, 5.0f, 80.0f);
        }

        // Right-drag → pan camera
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
            camX_ -= d.x * 0.03f;
            camY_ -= d.y * 0.03f;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
        }

        float mx = io.MousePos.x - vpScreenX_;
        float my = io.MousePos.y - vpScreenY_;

        // Left-click → use current tool (only in Edit mode)
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            editorMode_ == EditorMode::Edit) {
            float wx, wy;
            if (viewportScreenToWorld(mx, my, wx, wy)) {
                handleToolClick(wx, wy);
            }
        }

        // Entity drag-to-move (Select tool, Edit mode) ──────────────────────
        if (editorMode_ == EditorMode::Edit && currentTool_ == Tool::Select &&
            selectedEntityId_ != 0)
        {
            // Begin drag when left mouse button first pressed over entity
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                float wx, wy;
                if (viewportScreenToWorld(mx, my, wx, wy)) {
                    EntityData* ep = findEntityById(selectedEntityId_);
                    if (ep) {
                        float dx = ep->x - wx;
                        float dy = ep->y - wy;
                        if (std::sqrt(dx*dx + dy*dy) < 1.5f) {
                            draggingEntity_ = true;
                            dragStartX_ = ep->x;
                            dragStartY_ = ep->y;
                        }
                    }
                }
            }

            // While dragging: update position live (no command yet)
            if (draggingEntity_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                float wx, wy;
                if (viewportScreenToWorld(mx, my, wx, wy)) {
                    EntityData* ep = findEntityById(selectedEntityId_);
                    if (ep) {
                        ep->x = wx;
                        ep->y = wy;
                    }
                }
                SDL_SetCursor(cursorMove_);
            }

            // On release: commit as a command (supports undo/redo)
            if (draggingEntity_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                EntityData* ep = findEntityById(selectedEntityId_);
                if (ep) {
                    float newX = ep->x;
                    float newY = ep->y;
                    // Only create command if position actually changed
                    if (newX != dragStartX_ || newY != dragStartY_) {
                        // Restore original so command apply() sets the new pos
                        ep->x = dragStartX_;
                        ep->y = dragStartY_;
                        auto cmd = std::make_unique<MoveEntityCommand>(
                            selectedEntityId_,
                            dragStartX_, dragStartY_,
                            newX, newY);
                        commandStack_.execute(std::move(cmd), scene_, world_);
                    }
                }
                draggingEntity_ = false;
            }
        }

        // Cancel drag if focus lost
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            draggingEntity_ = false;

        // Continuous painting while dragging (Edit mode only)
        if (editorMode_ == EditorMode::Edit &&
            (currentTool_ == Tool::PaintTile || currentTool_ == Tool::HeightBrush) &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            float wx, wy;
            if (viewportScreenToWorld(mx, my, wx, wy)) {
                if (currentTool_ == Tool::PaintTile)
                    paintTileAt(wx, wy);
                else
                    heightBrushAt(wx, wy);
            }
        }
    } else {
        // Restore default arrow cursor outside viewport
        SDL_SetCursor(cursorArrow_);
    }

    // ── Marquee selection ────────────────────────────────────────────────────
    // After the tool handling above so an entity drag wins over the rectangle,
    // and outside the hover branch so a drag that leaves the image still ends.
    {
        const ImGuiIO& rio = ImGui::GetIO();
        updateRectSelection(gizmoViewProj, gizmoRect, rio.MousePos.x, rio.MousePos.y,
                            vpHovered, gizmoOwnsPointer);
    }

    // Play-mode overlay indicator
    if (editorMode_ == EditorMode::Play) {
        const std::string overlay = playback_.paused()
            ? "PAUSED " + playbackSpeedLabel()
            : "PLAYING " + playbackSpeedLabel();
        ImVec2 wp = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            {wp.x + 8, wp.y + 30}, {wp.x + 140, wp.y + 56},
            IM_COL32(200, 40, 40, 200), 4.f);
        ImGui::GetWindowDrawList()->AddText(
            {wp.x + 16, wp.y + 34}, IM_COL32(255, 255, 255, 255), overlay.c_str());
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

// ── Tool click dispatch ──────────────────────────────────────────────────────
void EditorApp::handleToolClick(float wx, float wy)
{
    switch (currentTool_) {
    case Tool::PaintTile:
        paintTileAt(wx, wy);
        break;

    case Tool::FillTile:
        floodFillAt(wx, wy);
        break;

    case Tool::EyeDropper: {
        int tx = (int)wx, ty = (int)wy;
        if (tx >= 0 && tx < WORLD_W && ty >= 0 && ty < WORLD_H) {
            selectedTileType_ = world_.terrain().face(tx, ty).type;
            currentTool_ = Tool::PaintTile;
        }
        break;
    }

    case Tool::HeightBrush: {
        heightBrushAt(wx, wy);
        break;
    }

    case Tool::CliffBrush: {
        cliffBrushAt(wx, wy);
        break;
    }

    case Tool::TexturePaint: {
        texturePaintAt(wx, wy);
        break;
    }

    case Tool::WaterTool: {
        waterToolAt(wx, wy);
        break;
    }

    case Tool::PlaceEnemy: {
        uint64_t newId = scene_.allocateEntityId();
        auto cmd = std::make_unique<PlaceEnemyCommand>(wx, wy, newId, "Enemy");
        commandStack_.execute(std::move(cmd), scene_, world_);
        setSelection(newId);
        addLog("Placed enemy.");
        break;
    }

    case Tool::Select: {
        uint64_t hit = 0;
        float best = 2.f;
        for (auto& e : scene_.entities) {
            const dash::editor::Transform3D w = dash::editor::worldTransform(scene_, e.id);
            float dx = w.x - wx;
            float dy = w.y - wy;
            float d  = std::sqrt(dx * dx + dy * dy);
            if (d < best) { best = d; hit = e.id; }
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeySuper || io.KeyCtrl) {
            if (hit != 0) toggleSelection(hit);
        } else {
            setSelection(hit);
        }
        break;
    }

    case Tool::Erase: {
        float    best     = 2.f;
        uint64_t eraseId  = 0;
        for (auto& e : scene_.entities) {
            if (e.type == EntityData::Type::Player) continue;
            float dx = e.x - wx;
            float dy = e.y - wy;
            float d  = std::sqrt(dx * dx + dy * dy);
            if (d < best) { best = d; eraseId = e.id; }
        }
        if (eraseId != 0) {
            auto cmd = std::make_unique<EraseCommand>(eraseId);
            commandStack_.execute(std::move(cmd), scene_, world_);
            clearSelection();
        }
        break;
    }
    }
}

void EditorApp::paintTileAt(float wx, float wy)
{
    int cx = (int)wx, cy = (int)wy;
    int r = brushSize_ - 1;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            int tx = cx + dx, ty = cy + dy;
            if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) continue;
            if (world_.grid[ty][tx].type == selectedTileType_) continue;
            auto cmd = std::make_unique<PaintTileCommand>(tx, ty, selectedTileType_);
            commandStack_.execute(std::move(cmd), scene_, world_);
        }
    }
}

void EditorApp::floodFillAt(float wx, float wy)
{
    int tx = (int)wx, ty = (int)wy;
    if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) return;
    if (world_.terrain().face(tx, ty).type == selectedTileType_) return;

    auto cmd = std::make_unique<FloodFillCommand>(tx, ty, selectedTileType_);
    commandStack_.execute(std::move(cmd), scene_, world_);
    addLog("Flood fill applied.");
}

void EditorApp::heightBrushAt(float wx, float wy)
{
    // Find nearest vertex
    int vx = static_cast<int>(std::round(wx));
    int vy = static_cast<int>(std::round(wy));
    if (vx < 0 || vx >= TerrainMesh::VW || vy < 0 || vy >= TerrainMesh::VH) return;

    HeightBrushCommand::Mode mode;
    switch (heightBrushMode_) {
    case HeightBrushMode::Raise:   mode = HeightBrushCommand::Mode::Raise;   break;
    case HeightBrushMode::Lower:   mode = HeightBrushCommand::Mode::Lower;   break;
    case HeightBrushMode::Smooth:  mode = HeightBrushCommand::Mode::Smooth;  break;
    case HeightBrushMode::Flatten: mode = HeightBrushCommand::Mode::Flatten; break;
    }

    auto cmd = std::make_unique<HeightBrushCommand>(
        vx, vy, heightBrushRadius_, heightBrushStrength_, mode);
    commandStack_.execute(std::move(cmd), scene_, world_);
}

void EditorApp::cliffBrushAt(float wx, float wy)
{
    int vx = static_cast<int>(std::round(wx));
    int vy = static_cast<int>(std::round(wy));
    if (vx < 0 || vx >= TerrainMesh::VW || vy < 0 || vy >= TerrainMesh::VH) return;

    auto cmd = std::make_unique<CliffBrushCommand>(
        vx, vy, cliffBrushRadius_, cliffBrushMode_);
    commandStack_.execute(std::move(cmd), scene_, world_);
}

void EditorApp::texturePaintAt(float wx, float wy)
{
    int vx = static_cast<int>(std::round(wx));
    int vy = static_cast<int>(std::round(wy));
    if (vx < 0 || vx >= TerrainMesh::VW || vy < 0 || vy >= TerrainMesh::VH) return;

    auto cmd = std::make_unique<TexturePaintCommand>(
        vx, vy, texturePaintRadius_, texturePaintStrength_, selectedTexture_);
    commandStack_.execute(std::move(cmd), scene_, world_);
}

void EditorApp::waterToolAt(float wx, float wy)
{
    // Water tool sets/adjusts water level for the selected water body
    TerrainMesh& tm = world_.terrain();
    bool found = false;
    for (auto& wb : tm.waterBodies()) {
        if (wb.id == static_cast<uint8_t>(selectedWaterBodyId_)) {
            float oldLevel = wb.waterLevel;
            auto cmd = std::make_unique<WaterLevelCommand>(
                wb.id, oldLevel, waterLevel_);
            commandStack_.execute(std::move(cmd), scene_, world_);
            found = true;
            break;
        }
    }
    if (!found) {
        // Create a new water body
        WaterBody wb;
        wb.id = static_cast<uint8_t>(selectedWaterBodyId_);
        wb.waterLevel = waterLevel_;
        wb.opacity = 0.6f;
        wb.tint = {0.08f, 0.14f, 0.31f};
        tm.addWaterBody(wb);
        tm.markDirty();
        scene_.modified = true;
    }
}

void EditorApp::getSpritePivot(const std::string& spriteName, float& outPivotX, float& outPivotY)
{
    outPivotX = 0.5f;
    outPivotY = 1.0f;

    fs::path metaPath = fs::path(assetsRoot_) / "sprites" / (spriteName + ".sprite.json");
    std::error_code ec;
    bool exists = fs::exists(metaPath, ec);
    if (!exists || ec) return;

    auto nowMtime = fs::last_write_time(metaPath, ec);
    if (ec) return;

    auto key = metaPath.string();
    auto it = spritePivotCache_.find(key);
    if (it != spritePivotCache_.end() && it->second.hasMtime && it->second.mtime == nowMtime) {
        outPivotX = it->second.pivotX;
        outPivotY = it->second.pivotY;
        return;
    }

    SpritePivotMeta meta;
    meta.hasMtime = true;
    meta.mtime = nowMtime;

    std::ifstream in(metaPath);
    if (in) {
        try {
            json j;
            in >> j;
            if (j.contains("pivotX") && j["pivotX"].is_number())
                meta.pivotX = j["pivotX"].get<float>();
            if (j.contains("pivotY") && j["pivotY"].is_number())
                meta.pivotY = j["pivotY"].get<float>();
        } catch (...) {
            // Keep defaults if metadata is invalid.
        }
    }

    meta.pivotX = std::clamp(meta.pivotX, 0.f, 1.f);
    meta.pivotY = std::clamp(meta.pivotY, 0.f, 1.f);
    spritePivotCache_[key] = meta;
    outPivotX = meta.pivotX;
    outPivotY = meta.pivotY;
}

Vec2f EditorApp::worldToScreenIso3D(float wx, float wy, float wz) const
{
    const float rx = (wx - camX_) * TILE_SCALE;
    const float ry = (wy - camY_) * TILE_SCALE;
    const float zoom = std::max(0.1f, viewport3D_.zoom);
    const float hw = (TILE_W * 0.5f) * zoom;
    const float hh = (TILE_H * 0.5f) * zoom;
    // wz is in world height units; heightScale / 8.0 converts old display scale
    return {
        (rx - ry) * hw + SCREEN_W * 0.5f,
        (rx + ry) * hh - (wz * TILE_SCALE * (viewport3D_.heightScale / 8.0f) * zoom) + SCREEN_H * 0.5f
    };
}

float EditorApp::tileHeight(TileType type) const
{
    switch (type) {
        case TileType::DeepWater: return -0.30f;
        case TileType::Water:     return -0.16f;
        case TileType::Sand:      return 0.00f;
        case TileType::Grass:     return 0.05f;
        case TileType::Forest:    return 0.14f;
        case TileType::Dirt:      return 0.08f;
        case TileType::Stone:     return 0.22f;
        case TileType::Mountain:  return 0.42f;
        case TileType::Snow:      return 0.50f;
    }
    return 0.0f;
}

float EditorApp::entityWorldZ(uint64_t entityId) const
{
    for (const auto& e : scene_.entities) {
        if (e.id != entityId) continue;
        for (const auto& c : e.components) {
            if (const auto* tf = std::get_if<TransformComponent>(&c)) return tf->z;
        }
        return 0.0f;
    }
    return 0.0f;
}

bool EditorApp::syncSceneRender3DSettingsFromUI()
{
    scene_.render3d.useVulkan3D = viewport3D_.useVulkan3D;
    scene_.render3d.embeddedPreview = viewport3D_.embeddedPreview;
    scene_.render3d.isoYawDeg = viewport3D_.isoYawDeg;
    scene_.render3d.isoPitchDeg = viewport3D_.isoPitchDeg;
    scene_.render3d.cameraDistance = viewport3D_.cameraDistance;
    scene_.render3d.cameraHeight = viewport3D_.cameraHeight;
    scene_.render3d.zoom = viewport3D_.zoom;
    scene_.render3d.heightScale = viewport3D_.heightScale;
    scene_.render3d.gridOpacity = viewport3D_.gridOpacity;
    return true;
}

void EditorApp::syncUIRender3DSettingsFromScene()
{
    viewport3D_.useVulkan3D = scene_.render3d.useVulkan3D;
    viewport3D_.embeddedPreview = scene_.render3d.embeddedPreview;
    viewport3D_.isoYawDeg = scene_.render3d.isoYawDeg;
    viewport3D_.isoPitchDeg = scene_.render3d.isoPitchDeg;
    viewport3D_.cameraDistance = scene_.render3d.cameraDistance;
    viewport3D_.cameraHeight = scene_.render3d.cameraHeight;
    viewport3D_.zoom = scene_.render3d.zoom;
    viewport3D_.heightScale = scene_.render3d.heightScale;
    viewport3D_.gridOpacity = scene_.render3d.gridOpacity;
}

// ═════════════════════════════════════════════════════════════════════════════
// Shared camera matrix builder (used by renderWorldToTexture & viewportScreenToWorld)
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::buildViewProjMatrix(float vpW, float vpH, float viewProj[16],
                                     float* outEyeX, float* outEyeY, float* outEyeZ)
{
    const float yaw   = viewport3D_.isoYawDeg * 3.14159265f / 180.0f;
    const float pitch = viewport3D_.isoPitchDeg * 3.14159265f / 180.0f;
    const float dist  = viewport3D_.cameraDistance;

    float targetX = camX_ * TILE_SCALE;
    float targetZ = camY_ * TILE_SCALE;
    float targetY = viewport3D_.cameraHeight;

    float eyeX = targetX + dist * std::cos(yaw) * std::cos(pitch);
    float eyeY = targetY + dist * std::sin(pitch);
    float eyeZ = targetZ + dist * std::sin(yaw) * std::cos(pitch);

    if (outEyeX) *outEyeX = eyeX;
    if (outEyeY) *outEyeY = eyeY;
    if (outEyeZ) *outEyeZ = eyeZ;

    // Look-at matrix
    float fx = targetX - eyeX, fy = targetY - eyeY, fz = targetZ - eyeZ;
    float flen = std::sqrt(fx*fx + fy*fy + fz*fz);
    if (flen > 1e-6f) { fx /= flen; fy /= flen; fz /= flen; }

    float ux = 0.0f, uy = 1.0f, uz = 0.0f;
    float rx = fy * uz - fz * uy;
    float ry = fz * ux - fx * uz;
    float rz = fx * uy - fy * ux;
    float rlen = std::sqrt(rx*rx + ry*ry + rz*rz);
    if (rlen > 1e-6f) { rx /= rlen; ry /= rlen; rz /= rlen; }
    ux = ry * fz - rz * fy;
    uy = rz * fx - rx * fz;
    uz = rx * fy - ry * fx;

    float view[16] = {
         rx,  ux, -fx, 0,
         ry,  uy, -fy, 0,
         rz,  uz, -fz, 0,
        -(rx*eyeX + ry*eyeY + rz*eyeZ),
        -(ux*eyeX + uy*eyeY + uz*eyeZ),
        -(-fx*eyeX + -fy*eyeY + -fz*eyeZ),
         1
    };

    float aspect = vpW / vpH;
    float fov = 45.0f * 3.14159265f / 180.0f;
    float nearP = 0.1f, farP = 500.0f;
    float tanHalf = std::tan(fov * 0.5f);
    float proj[16] = {
        1.0f / (aspect * tanHalf), 0, 0, 0,
        0, -1.0f / tanHalf, 0, 0,
        0, 0, farP / (nearP - farP), -1,
        0, 0, (farP * nearP) / (nearP - farP), 0
    };

    // viewProj = proj * view (column-major)
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            viewProj[c * 4 + r] =
                proj[0 * 4 + r] * view[c * 4 + 0] +
                proj[1 * 4 + r] * view[c * 4 + 1] +
                proj[2 * 4 + r] * view[c * 4 + 2] +
                proj[3 * 4 + r] * view[c * 4 + 3];
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Viewport rendering (Vulkan pipeline)
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::renderWorldToTexture()
{
    // Determine viewport size from the ImGui panel
    uint32_t vpW = static_cast<uint32_t>(std::max(1.0f, vpDisplayW_));
    uint32_t vpH = static_cast<uint32_t>(std::max(1.0f, vpDisplayH_));

    // ── Update terrain mesh if dirty ────────────────────────────────────────
    vkCtx_.updateTerrainMesh(world_.terrain());

    // ── Build view-projection matrix (isometric 3D camera) ──────────────────
    float eyeX, eyeY, eyeZ;
    float viewProj[16];
    buildViewProjMatrix(static_cast<float>(vpW), static_cast<float>(vpH),
                        viewProj, &eyeX, &eyeY, &eyeZ);

    vkCtx_.updateCamera(viewProj);

    // ── Scene instances and lights ──────────────────────────────────────────
    // Built up front because the shadow depth pass draws the very same casters,
    // and it has to be recorded before the viewport pass: passes cannot nest.
    const SceneData flatScene = dash::editor::flattenHierarchy(scene_);
    std::vector<dash::vkexp::RenderInstance> instances =
        dash::vkexp::SceneLoader::buildInstances(flatScene);
    std::vector<dash::vkexp::SceneLight> sceneLights =
        dash::vkexp::SceneLoader::buildLights(flatScene);

    std::vector<dash::vkexp::InstanceResources> resources(instances.size());
    for (size_t i = 0; i < instances.size(); ++i) {
        auto& inst = instances[i];
        inst.position.y += world_.terrain().sampleHeight(inst.position.x, inst.position.z);
        resources[i].mesh = vkCtx_.resolveMesh(inst.meshId);
        // Enemies fall back to the wolf model, as before, when no mesh is set.
        if (!resources[i].mesh && !inst.isPlayer && vkCtx_.wolfMesh().indexCount() > 0)
            resources[i].mesh = &vkCtx_.wolfMesh();
    }

    dash::vkexp::LightingParams lighting;
    lighting.dirX = viewport3D_.lightDirX;
    lighting.dirY = viewport3D_.lightDirY;
    lighting.dirZ = viewport3D_.lightDirZ;
    lighting.intensity = viewport3D_.lightIntensity;
    lighting.colorR = viewport3D_.lightColorR;
    lighting.colorG = viewport3D_.lightColorG;
    lighting.colorB = viewport3D_.lightColorB;
    lighting.ambient = viewport3D_.ambientStrength;

    // Same sun Renderer::init synthesizes when a scene declares no light, so the
    // Lighting panel drives the viewport shadows live. LightingParams.dir is the
    // surface-to-light vector, SceneLight.dir the emission direction.
    if (sceneLights.empty()) {
        dash::vkexp::SceneLight sun;
        sun.type = 0;
        sun.dirX = -lighting.dirX;
        sun.dirY = -lighting.dirY;
        sun.dirZ = -lighting.dirZ;
        sun.colorR = lighting.colorR;
        sun.colorG = lighting.colorG;
        sun.colorB = lighting.colorB;
        sun.intensity = lighting.intensity;
        sun.castsShadows = true;
        sceneLights.push_back(sun);
    }

    // ── Shadow cascades ─────────────────────────────────────────────────────
    int shadowLight = -1;
    for (size_t i = 0; i < sceneLights.size(); ++i) {
        if (sceneLights[i].type == 0 && sceneLights[i].castsShadows) {
            shadowLight = static_cast<int>(i);
            break;
        }
    }

    // Same basis buildViewProjMatrix derives, reused for the frustum slices and
    // for the billboard axes further down.
    const dash::vkexp::Vec3 forward = dash::vkexp::normalize(
        {camX_ * TILE_SCALE - eyeX, viewport3D_.cameraHeight - eyeY, camY_ * TILE_SCALE - eyeZ});
    const dash::vkexp::Vec3 camRight = dash::vkexp::normalize(
        dash::vkexp::cross(forward, {0.0f, 1.0f, 0.0f}));
    const dash::vkexp::Vec3 camUp = dash::vkexp::cross(camRight, forward);

    dash::vkexp::Vec3 shadowDir{-lighting.dirX, -lighting.dirY, -lighting.dirZ};
    if (shadowLight >= 0) {
        const dash::vkexp::SceneLight& l = sceneLights[static_cast<size_t>(shadowLight)];
        shadowDir = {l.dirX, l.dirY, l.dirZ};
    }
    vkCtx_.updateShadowCascades({eyeX, eyeY, eyeZ}, forward, camRight, camUp,
                                45.0f * 3.14159265f / 180.0f,
                                static_cast<float>(vpW) / static_cast<float>(vpH),
                                shadowDir, shadowLight);

    // Scene lights need the "_lit"/"_shadow" pipeline; without it the viewport
    // keeps the flat directional shading it always had.
    const bool useSceneLights = vkCtx_.basicLitPipeline() != VK_NULL_HANDLE;

    // Uploaded even when the lit pipeline is missing: the terrain shader reads
    // the camera position and the cascade block out of the same buffer.
    dash::vkexp::SceneLightsUbo lightUbo;
    const int lightCount = dash::vkexp::packSceneLights(
        &sceneLights, {eyeX, eyeY, eyeZ}, lightUbo);
    vkCtx_.fillShadowUbo(lightUbo);
    vkCtx_.updateSceneLights(lightUbo, lightCount);

    // ── Shadow depth pass (outside the viewport render pass) ────────────────
    vkCtx_.recordShadowPass(instances, resources);

    // ── Begin offscreen viewport render pass ────────────────────────────────
    vkCtx_.beginViewportRender(vpW, vpH);
    VkCommandBuffer cmd = vkCtx_.currentCmd();

    // ── Terrain ─────────────────────────────────────────────────────────────
    if (vkCtx_.terrainMesh().indexCount() > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkCtx_.terrainPipeline());
        VkDescriptorSet ds = vkCtx_.sceneDescriptorSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                vkCtx_.terrainPipelineLayout(), 0, 1, &ds, 0, nullptr);

        VkBuffer vb[] = { vkCtx_.terrainMesh().vertexBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, vkCtx_.terrainMesh().indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        // Push constants: eyePos(3) + time(1) + fogStart(1) + fogEnd(1) + lightDir(3) + intensity(1) + lightColor(3) + ambient(1) + 2 spare, then the per-layer roughness table
        static auto startTime = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();

        float layerRoughness[dash::vkexp::kTerrainRoughnessFloats];
        dash::vkexp::packTerrainLayerRoughness(layerRoughness);

        float terrainPC[dash::vkexp::kTerrainPushConstantFloats] = {
            eyeX, eyeY, eyeZ, elapsed,
            viewport3D_.fogStart, viewport3D_.fogEnd, viewport3D_.lightDirX, viewport3D_.lightDirY,
            viewport3D_.lightDirZ, viewport3D_.lightIntensity, viewport3D_.lightColorR, viewport3D_.lightColorG,
            viewport3D_.lightColorB, viewport3D_.ambientStrength, 0.0f, 0.0f
        };
        std::copy(std::begin(layerRoughness), std::end(layerRoughness), terrainPC + 16);
        vkCmdPushConstants(cmd, vkCtx_.terrainPipelineLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(terrainPC), terrainPC);

        vkCmdDrawIndexed(cmd, vkCtx_.terrainMesh().indexCount(), 1, 0, 0, 0);
    }

    // ── Water ───────────────────────────────────────────────────────────────
    if (vkCtx_.waterMesh().indexCount() > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkCtx_.waterPipeline());
        VkDescriptorSet ds = vkCtx_.sceneDescriptorSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                vkCtx_.waterPipelineLayout(), 0, 1, &ds, 0, nullptr);

        VkBuffer vb[] = { vkCtx_.waterMesh().vertexBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, vkCtx_.waterMesh().indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        static auto startTime2 = std::chrono::high_resolution_clock::now();
        float elapsed2 = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime2).count();
        float waterPC[dash::vkexp::kTerrainPushConstantFloats] = {
            eyeX, eyeY, eyeZ, elapsed2,
            viewport3D_.fogStart, viewport3D_.fogEnd, viewport3D_.lightDirX, viewport3D_.lightDirY,
            viewport3D_.lightDirZ, viewport3D_.lightIntensity, viewport3D_.lightColorR, viewport3D_.lightColorG,
            viewport3D_.lightColorB, viewport3D_.ambientStrength, 0.0f, 0.0f
        };
        vkCmdPushConstants(cmd, vkCtx_.waterPipelineLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(waterPC), waterPC);

        vkCmdDrawIndexed(cmd, vkCtx_.waterMesh().indexCount(), 1, 0, 0, 0);
    }

    // ── Entity rendering ─────────────────────────────────────────────────────
    if (vkCtx_.cubeMesh().indexCount() > 0) {
        VkPipeline       opaquePipeline = useSceneLights ? vkCtx_.basicLitPipeline()
                                                         : vkCtx_.basicPipeline();
        VkPipelineLayout opaqueLayout   = useSceneLights ? vkCtx_.basicLitPipelineLayout()
                                                         : vkCtx_.basicPipelineLayout();

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, opaquePipeline);
        VkDescriptorSet ds = vkCtx_.sceneDescriptorSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                opaqueLayout, 0, 1, &ds, 0, nullptr);

        VkBuffer vb[] = { vkCtx_.cubeMesh().vertexBuffer() };
        VkDeviceSize vbOffsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, vbOffsets);
        vkCmdBindIndexBuffer(cmd, vkCtx_.cubeMesh().indexBuffer(), 0, vkCtx_.cubeMesh().indexType());

        dash::vkexp::SceneDrawParams params;
        params.opaquePipeline    = opaquePipeline;
        params.opaqueLayout      = opaqueLayout;
        params.billboardPipeline = vkCtx_.billboardPipeline();
        params.billboardLayout   = vkCtx_.billboardPipelineLayout();
        params.defaultSet        = ds;
        params.fallbackMesh      = &vkCtx_.cubeMesh();
        params.lights            = useSceneLights ? &sceneLights : nullptr;
        params.cameraRight       = camRight;
        params.cameraUp          = camUp;
        std::memcpy(params.viewProj.m, viewProj, sizeof(params.viewProj.m));

        dash::vkexp::drawSceneInstances(cmd, instances, resources, lighting, params);
    }

    vkCtx_.endViewportRender();
}

bool EditorApp::viewportScreenToWorld(float vx, float vy, float& wx, float& wy)
{
    // Build the same viewProj used for rendering
    float viewProj[16];
    buildViewProjMatrix(vpDisplayW_, vpDisplayH_, viewProj);

    // Invert the viewProj matrix (4x4 cofactor inverse)
    float inv[16];
    {
        const float* m = viewProj;
        float a00 = m[0], a01 = m[1], a02 = m[2],  a03 = m[3];
        float a10 = m[4], a11 = m[5], a12 = m[6],  a13 = m[7];
        float a20 = m[8], a21 = m[9], a22 = m[10], a23 = m[11];
        float a30 = m[12],a31 = m[13],a32 = m[14], a33 = m[15];

        float b00 = a00*a11 - a01*a10, b01 = a00*a12 - a02*a10;
        float b02 = a00*a13 - a03*a10, b03 = a01*a12 - a02*a11;
        float b04 = a01*a13 - a03*a11, b05 = a02*a13 - a03*a12;
        float b06 = a20*a31 - a21*a30, b07 = a20*a32 - a22*a30;
        float b08 = a20*a33 - a23*a30, b09 = a21*a32 - a22*a31;
        float b10 = a21*a33 - a23*a31, b11 = a22*a33 - a23*a32;

        float det = b00*b11 - b01*b10 + b02*b09 + b03*b08 - b04*b07 + b05*b06;
        if (std::abs(det) < 1e-12f) return false;
        float invDet = 1.0f / det;

        inv[0]  = ( a11*b11 - a12*b10 + a13*b09) * invDet;
        inv[1]  = (-a01*b11 + a02*b10 - a03*b09) * invDet;
        inv[2]  = ( a31*b05 - a32*b04 + a33*b03) * invDet;
        inv[3]  = (-a21*b05 + a22*b04 - a23*b03) * invDet;
        inv[4]  = (-a10*b11 + a12*b08 - a13*b07) * invDet;
        inv[5]  = ( a00*b11 - a02*b08 + a03*b07) * invDet;
        inv[6]  = (-a30*b05 + a32*b02 - a33*b01) * invDet;
        inv[7]  = ( a20*b05 - a22*b02 + a23*b01) * invDet;
        inv[8]  = ( a10*b10 - a11*b08 + a13*b06) * invDet;
        inv[9]  = (-a00*b10 + a01*b08 - a03*b06) * invDet;
        inv[10] = ( a30*b04 - a31*b02 + a33*b00) * invDet;
        inv[11] = (-a20*b04 + a21*b02 - a23*b00) * invDet;
        inv[12] = (-a10*b09 + a11*b07 - a12*b06) * invDet;
        inv[13] = ( a00*b09 - a01*b07 + a02*b06) * invDet;
        inv[14] = (-a30*b03 + a31*b01 - a32*b00) * invDet;
        inv[15] = ( a20*b03 - a21*b01 + a22*b00) * invDet;
    }

    // Convert viewport mouse coords to Vulkan NDC (Y: -1=top, +1=bottom)
    float ndcX = (2.0f * vx / vpDisplayW_) - 1.0f;
    float ndcY = (2.0f * vy / vpDisplayH_) - 1.0f;

    // Unproject near and far points through inverse viewProj
    auto unproject = [&](float ndcZ, float& outX, float& outY, float& outZ) {
        float x = inv[0]*ndcX + inv[4]*ndcY + inv[8]*ndcZ  + inv[12];
        float y = inv[1]*ndcX + inv[5]*ndcY + inv[9]*ndcZ  + inv[13];
        float z = inv[2]*ndcX + inv[6]*ndcY + inv[10]*ndcZ + inv[14];
        float w = inv[3]*ndcX + inv[7]*ndcY + inv[11]*ndcZ + inv[15];
        if (std::abs(w) < 1e-12f) return false;
        outX = x / w; outY = y / w; outZ = z / w;
        return true;
    };

    float nearX, nearY, nearZ, farX, farY, farZ;
    if (!unproject(0.0f, nearX, nearY, nearZ)) return false;  // Vulkan near = 0
    if (!unproject(1.0f, farX,  farY,  farZ))  return false;  // Vulkan far = 1

    // Ray direction
    float dirX = farX - nearX, dirY = farY - nearY, dirZ = farZ - nearZ;

    // Intersect ray with terrain plane y = 0
    if (std::abs(dirY) < 1e-6f) return false;  // ray parallel to ground
    float t = -nearY / dirY;
    if (t < 0.0f) return false;  // intersection behind camera

    float hitX = nearX + t * dirX;
    float hitZ = nearZ + t * dirZ;

    // Convert from 3D world space to tile coords
    wx = hitX / TILE_SCALE;
    wy = hitZ / TILE_SCALE;
    return (wx >= 0 && wx < WORLD_W && wy >= 0 && wy < WORLD_H);
}

// ═════════════════════════════════════════════════════════════════════════════
// Build Log
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawBuildLog()
{
    ImGui::Begin("Build Log");

    if (ImGui::CollapsingHeader("Play Audit (ultimas 2 sesiones)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::string auditPath = playAuditFilePath();
        ImGui::TextDisabled("Archivo: %s", auditPath.c_str());

        json auditRoot;
        auditRoot["sessions"] = json::array();

        std::ifstream in(auditPath);
        if (in.is_open()) {
            try {
                in >> auditRoot;
            } catch (...) {
                auditRoot = json{};
                auditRoot["sessions"] = json::array();
            }
        }

        const bool hasSessions = auditRoot.contains("sessions")
                                 && auditRoot["sessions"].is_array()
                                 && !auditRoot["sessions"].empty();

        if (!hasSessions) {
            ImGui::TextDisabled("Sin sesiones auditadas todavia.");
        } else {
            const auto& sessions = auditRoot["sessions"];
            int shown = 0;
            for (int i = static_cast<int>(sessions.size()) - 1; i >= 0 && shown < 2; --i, ++shown) {
                const auto& s = sessions[static_cast<size_t>(i)];
                const std::string startedAt = s.value("startedAt", "unknown");
                const std::string endedAt = s.value("endedAt", "unknown");
                const std::string reason = s.value("reason", "unknown");

                const std::string title = "Sesion " + std::to_string(shown + 1)
                                        + " | " + startedAt
                                        + "##audit_session_" + std::to_string(i);
                if (ImGui::TreeNode(title.c_str())) {
                    ImGui::Text("Inicio: %s", startedAt.c_str());
                    ImGui::Text("Fin: %s", endedAt.c_str());
                    ImGui::Text("Motivo: %s", reason.c_str());

                    if (s.contains("logs") && s["logs"].is_array()) {
                        ImGui::SeparatorText("Logs de sesion");
                        ImGui::BeginChild(("audit_logs_" + std::to_string(i)).c_str(), ImVec2(-FLT_MIN, 140.0f), true);
                        for (const auto& line : s["logs"]) {
                            if (line.is_string()) {
                                ImGui::TextUnformatted(line.get_ref<const std::string&>().c_str());
                            }
                        }
                        ImGui::EndChild();
                    }
                    ImGui::TreePop();
                }
            }
        }

        ImGui::Separator();
    }

    ImGui::TextDisabled("Log en vivo del editor");

    std::string combinedLog;
    size_t totalSize = 0;
    for (const auto& msg : log_)
        totalSize += msg.size() + 1;
    combinedLog.reserve(totalSize);

    for (const auto& msg : log_) {
        combinedLog += msg;
        combinedLog += '\n';
    }

    const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
    ImGui::InputTextMultiline("##build_log_text",
                              combinedLog.data(),
                              combinedLog.size() + 1,
                              ImVec2(-FLT_MIN, -FLT_MIN),
                              ImGuiInputTextFlags_ReadOnly);
    if (wasAtBottom)
        ImGui::SetScrollHereY(1.f);
    ImGui::End();
}

void EditorApp::drawPerformancePanel()
{
    ImGui::Begin("Performance");

    auto& prof = Profiler::instance();
    ImGui::Text("FPS: %.1f", prof.fps());
    ImGui::Text("Frame: %.2f ms  (avg %.2f ms, peak %.2f ms)",
                prof.frameDtMs(), prof.frameAvgMs(), prof.framePeakMs());

    ImGui::Separator();
    ImGui::Text("Subsystems:");

    if (ImGui::BeginTable("##PerfTable", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Section");
        ImGui::TableSetupColumn("Last (ms)");
        ImGui::TableSetupColumn("Avg (ms)");
        ImGui::TableSetupColumn("Peak (ms)");
        ImGui::TableHeadersRow();

        for (auto& s : prof.sections()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(s.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", s.lastMs);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", s.avgMs);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", s.peakMs);
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════════
// Lighting Panel
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawLightingPanel()
{
    ImGui::Begin("Lighting");

    ImGui::SeparatorText("Directional Light");

    // Direction
    float dir[3] = { viewport3D_.lightDirX, viewport3D_.lightDirY, viewport3D_.lightDirZ };
    ImGui::Text("Direction");
    if (ImGui::SliderFloat3("##lightdir", dir, -1.0f, 1.0f, "%.2f")) {
        viewport3D_.lightDirX = dir[0];
        viewport3D_.lightDirY = dir[1];
        viewport3D_.lightDirZ = dir[2];
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Normalize")) {
        float len = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
        if (len > 1e-4f) {
            viewport3D_.lightDirX = dir[0] / len;
            viewport3D_.lightDirY = dir[1] / len;
            viewport3D_.lightDirZ = dir[2] / len;
        }
    }

    // Color
    float col[3] = { viewport3D_.lightColorR, viewport3D_.lightColorG, viewport3D_.lightColorB };
    ImGui::Text("Color");
    if (ImGui::ColorEdit3("##lightcol", col)) {
        viewport3D_.lightColorR = col[0];
        viewport3D_.lightColorG = col[1];
        viewport3D_.lightColorB = col[2];
    }

    // Intensity
    ImGui::Text("Intensity");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::SliderFloat("##intensity", &viewport3D_.lightIntensity, 0.0f, 3.0f, "%.2f");

    ImGui::SeparatorText("Ambient");

    ImGui::Text("Ambient");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::SliderFloat("##ambient", &viewport3D_.ambientStrength, 0.0f, 1.0f, "%.2f");

    // Specular is no longer a global knob: it falls out of each material's
    // metallic/roughness through the Cook-Torrance BRDF.

    ImGui::Separator();
    if (ImGui::Button("Reset Defaults")) {
        viewport3D_.lightDirX = 0.3f;  viewport3D_.lightDirY = 0.9f;  viewport3D_.lightDirZ = 0.2f;
        viewport3D_.lightColorR = 1.0f; viewport3D_.lightColorG = 0.98f; viewport3D_.lightColorB = 0.92f;
        viewport3D_.lightIntensity = 1.7f;
        viewport3D_.ambientStrength = 0.30f;
    }

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════════
// File Dialogs
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawOpenDialog()
{
    ImGui::OpenPopup("Open Scene");
    if (ImGui::BeginPopupModal("Open Scene", &showOpenDialog_,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Scenes in project:");
        ImGui::Separator();

        for (auto& f : sceneFiles_) {
            if (ImGui::Selectable(f.c_str())) {
                openScene(scenesDir_ + "/" + f);
                showOpenDialog_ = false;
            }
        }
        if (sceneFiles_.empty())
            ImGui::TextDisabled("No .json files in scenes/");

        ImGui::Separator();
        if (ImGui::Button("Cancel", {120, 0})) {
            showOpenDialog_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::drawSaveDialog()
{
    ImGui::OpenPopup("Save Scene");
    if (ImGui::BeginPopupModal("Save Scene", &showSaveDialog_,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        char sceneName[128];
        std::strncpy(sceneName, scene_.sceneName.c_str(), sizeof(sceneName));
        sceneName[sizeof(sceneName) - 1] = '\0';
        if (ImGui::InputText("Scene Name", sceneName, sizeof(sceneName)))
            scene_.sceneName = sceneName;

        ImGui::InputText("File Name", saveFileName_, sizeof(saveFileName_));

        ImGui::Separator();
        if (ImGui::Button("Save", {120, 0})) {
            saveScene(scenesDir_ + "/" + saveFileName_);
            showSaveDialog_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0})) {
            showSaveDialog_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::drawCreateSceneDialog()
{
    ImGui::OpenPopup("Create Scene");
    if (ImGui::BeginPopupModal("Create Scene", &showCreateSceneDialog_,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Create a new scene file in scenes/");
        ImGui::Separator();

        ImGui::InputText("File Name", createSceneFileName_, sizeof(createSceneFileName_));

        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.17f, 0.55f, 0.24f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.68f, 0.31f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.14f, 0.45f, 0.21f, 1.f));
        if (ImGui::Button(ICON_FA_FILE " Create", {120, 0})) {
            std::string fileName(createSceneFileName_);
            if (fileName.empty()) {
                addLog("ERROR: Scene file name cannot be empty.");
            } else {
                if (fs::path(fileName).extension() != ".json")
                    fileName += ".json";

                const fs::path scenePath = fs::path(scenesDir_) / fileName;
                const bool sqliteScenes = projectManager_.hasActiveProject() && sqliteModeEnabled();
                const bool existsInSelection = std::find(sceneFiles_.begin(), sceneFiles_.end(), fileName) != sceneFiles_.end();
                if ((sqliteScenes && existsInSelection) || (!sqliteScenes && fs::exists(scenePath))) {
                    addLog("ERROR: Scene already exists: " + fileName);
                } else {
                    newScene();
                    scene_.sceneName = fs::path(fileName).stem().string();
                    saveScene(scenePath.string());
                    openScene(fileName);
                    showCreateSceneDialog_ = false;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.62f, 0.20f, 0.20f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.76f, 0.25f, 0.25f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.52f, 0.16f, 0.16f, 1.f));
        if (ImGui::Button("Cancel", {120, 0})) {
            showCreateSceneDialog_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);

        ImGui::EndPopup();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Actions
// ═════════════════════════════════════════════════════════════════════════════

// Center camera on the Player entity, or the centroid of all entities,
// falling back to the world center if no entities exist.
void EditorApp::focusCameraOnEntities()
{
    // Try Player first
    for (const auto& e : scene_.entities) {
        if (e.type == EntityData::Type::Player) {
            camX_ = e.x;
            camY_ = e.y;
            return;
        }
    }
    // Centroid of all entities
    if (!scene_.entities.empty()) {
        float sx = 0.f, sy = 0.f;
        for (const auto& e : scene_.entities) { sx += e.x; sy += e.y; }
        camX_ = sx / static_cast<float>(scene_.entities.size());
        camY_ = sy / static_cast<float>(scene_.entities.size());
        return;
    }
    // Fallback: world center
    camX_ = WORLD_W / 2.f;
    camY_ = WORLD_H / 2.f;
}

void EditorApp::newScene()
{
    scene_.createDefault();
    syncUIRender3DSettingsFromScene();
    world_.generate(scene_.worldSeed);
    clearSelection();
    commandStack_.clear();
    camX_ = WORLD_W / 2.f;
    camY_ = WORLD_H / 2.f;
    addLog("New scene created.");
}

void EditorApp::refreshSceneFiles()
{
    const std::string previousSelection = selectedSceneFile_;
    sceneFiles_.clear();

    if (projectManager_.hasActiveProject() && sqliteModeEnabled()) {
        const fs::path dbPath = projectSqlitePath(projectManager_.manifest());
        if (fs::exists(dbPath)) {
            SceneRepositorySqlite repo(dbPath.string());
            std::string error;
            if (repo.listSceneFiles(sceneFiles_, &error)) {
                std::sort(sceneFiles_.begin(), sceneFiles_.end());
            } else {
                addLog("[SCENE] SQLite scene listing failed, fallback to files: " + error);
                sceneFiles_.clear();
            }
        }
    }

    if (sceneFiles_.empty()) {
        if (!fs::exists(scenesDir_)) return;
        for (auto& entry : fs::directory_iterator(scenesDir_)) {
            if (entry.path().extension() == ".json")
                sceneFiles_.push_back(entry.path().filename().string());
        }
        std::sort(sceneFiles_.begin(), sceneFiles_.end());
    }

    if (sceneFiles_.empty()) {
        selectedSceneFile_.clear();
        return;
    }

    if (std::find(sceneFiles_.begin(), sceneFiles_.end(), previousSelection) != sceneFiles_.end()) {
        selectedSceneFile_ = previousSelection;
    } else {
        selectedSceneFile_ = sceneFiles_.front();
    }
}

void EditorApp::loadInitialProjectScene()
{
    addLog("[SCENE:LOAD] loadInitialProjectScene begin");
    if (!projectManager_.hasActiveProject()) {
        addLog("[SCENE:LOAD] NO ACTIVE PROJECT - creating new scene");
        return;
    }

    const auto& manifest = projectManager_.manifest();
    const fs::path defaultScenePath = manifest.absoluteDefaultScene();
    const std::string defaultSceneFile = defaultScenePath.filename().string();

    addLog("[SCENE:LOAD] Default scene from manifest: " + defaultSceneFile);
    addLog("[SCENE:LOAD] Absolute path: " + defaultScenePath.string());

    // Always prefer the project's declared default scene when it exists on disk.
    // In SQLite mode, scene listings may not contain JSON-backed scenes yet.
    if (fs::exists(defaultScenePath)) {
        addLog("[SCENE:LOAD] Default scene path EXISTS on disk");
        selectedSceneFile_ = defaultSceneFile;
        openScene(defaultScenePath.string());
        if (scene_.filePath.empty()) {
            addLog("[SCENE:LOAD] ERROR: Could not open default scene path: " + defaultScenePath.string());
        } else {
            addLog("[SCENE:LOAD] SUCCESS: Loaded default scene from disk: " + defaultScenePath.string());
            addLog("[SCENE:LOAD] Scene has " + std::to_string(scene_.entities.size()) + " entities");
            refreshSceneFiles();
            return;
        }
    } else {
        addLog("[SCENE:LOAD] Default scene path DOES NOT EXIST: " + defaultScenePath.string());
    }

    refreshSceneFiles();
    addLog("[SCENE:LOAD] Available scenes: " + std::to_string(sceneFiles_.size()));
    for (const auto& f : sceneFiles_) {
        addLog("[SCENE:LOAD]   - " + f);
    }
    
    if (!sceneFiles_.empty()) {
        if (std::find(sceneFiles_.begin(), sceneFiles_.end(), defaultSceneFile) != sceneFiles_.end()) {
            selectedSceneFile_ = defaultSceneFile;
            openScene(defaultSceneFile);
            addLog("[SCENE:LOAD] Auto-loaded default scene from list: " + defaultSceneFile);
            return;
        }

        selectedSceneFile_ = sceneFiles_.front();
        openScene(selectedSceneFile_);
        addLog("[SCENE:LOAD] Auto-loaded first available scene: " + selectedSceneFile_);
        addLog("[SCENE:LOAD] Scene has " + std::to_string(scene_.entities.size()) + " entities");
        return;
    }

    newScene();
    addLog("[SCENE:LOAD] No project scenes found; started a new scene.");
}


void EditorApp::saveScene(const std::string& path)
{
    // Validate before save
    bool hasPlayer = false;
    for (auto& e : scene_.entities)
        if (e.type == EntityData::Type::Player) { hasPlayer = true; break; }
    if (!hasPlayer) {
        addLog("WARNING: Scene has no Player entity. Saving anyway.");
    }

    // Capture current terrain state into scene overrides
    {
        TerrainMesh& tm = world_.terrain();

        // Cliff overrides: vertices with non-zero cliff level
        scene_.cliffOverrides.clear();
        for (int vy = 0; vy < TerrainMesh::VH; ++vy)
            for (int vx = 0; vx < TerrainMesh::VW; ++vx) {
                uint8_t cl = tm.vert(vx, vy).cliffLevel;
                if (cl != 0)
                    scene_.cliffOverrides.push_back({vx, vy, cl});
            }

        // Texture overrides: vertices with non-default texture blend
        scene_.textureOverrides.clear();
        for (int vy = 0; vy < TerrainMesh::VH; ++vy)
            for (int vx = 0; vx < TerrainMesh::VW; ++vx) {
                const auto& v = tm.vert(vx, vy);
                if (v.texWeights[1] != 0 || v.texWeights[2] != 0 || v.texWeights[3] != 0
                    || v.texIndices[0] != 0) {
                    TextureOverride to;
                    to.vx = vx; to.vy = vy;
                    std::memcpy(to.texIndices, v.texIndices, 4);
                    std::memcpy(to.texWeights, v.texWeights, 4);
                    scene_.textureOverrides.push_back(to);
                }
            }

        // Water bodies
        scene_.waterBodies = tm.waterBodies();
    }

    const std::string fileName = fs::path(path).filename().string();
    const bool sqliteScenes = projectManager_.hasActiveProject() && sqliteModeEnabled();

    if (sqliteScenes) {
        const fs::path dbPath = projectSqlitePath(projectManager_.manifest());
        SceneRepositorySqlite repo(dbPath.string());
        std::string error;
        if (repo.saveScene(fileName, scene_, &error)) {
            scene_.filePath = (fs::path(scenesDir_) / fileName).string();
            scene_.modified = false;
            addLog("Saved (SQLite): " + fileName + " (v" + std::to_string(SceneData::kCurrentVersion) + ")");
            refreshSceneFiles();
            selectedSceneFile_ = fileName;
            return;
        }
        addLog("[SCENE] SQLite save failed, fallback to JSON: " + error);
    }

    if (scene_.saveToFile(path)) {
        addLog("Saved: " + path + " (v" + std::to_string(SceneData::kCurrentVersion) + ")");
        refreshSceneFiles();
        selectedSceneFile_ = fileName;
    } else {
        addLog("ERROR: Could not write scene file: " + path);
    }
}

void EditorApp::openScene(const std::string& path)
{
    const std::string fileName = fs::path(path).filename().string();
    const bool sqliteScenes = projectManager_.hasActiveProject() && sqliteModeEnabled();

    if (sqliteScenes) {
        const fs::path dbPath = projectSqlitePath(projectManager_.manifest());
        if (fs::exists(dbPath)) {
            SceneRepositorySqlite repo(dbPath.string());
            std::string error;
            if (repo.loadScene(fileName, scene_, assetsRoot_, &error)) {
                scene_.filePath = (fs::path(scenesDir_) / fileName).string();
                syncUIRender3DSettingsFromScene();

                for (auto& err : scene_.loadErrors)
                    addLog("  [load] " + err);

                world_.generate(scene_.worldSeed);
                applySceneToWorld();
                clearSelection();
                commandStack_.clear();
                focusCameraOnEntities();
                selectedSceneFile_ = fileName;
                addLog("Loaded (SQLite): " + fileName + " (v" + std::to_string(scene_.sceneVersion) + ")");
                return;
            }
            addLog("[SCENE] SQLite load failed, fallback to JSON: " + error);
        }
    }

    if (scene_.loadFromFile(path, assetsRoot_)) {
        syncUIRender3DSettingsFromScene();
        // Report any warnings collected during load
        for (auto& err : scene_.loadErrors)
            addLog("  [load] " + err);

        world_.generate(scene_.worldSeed);
        applySceneToWorld();
        clearSelection();
        commandStack_.clear();
        focusCameraOnEntities();
        selectedSceneFile_ = fileName;
        addLog("Loaded: " + path + " (v" + std::to_string(scene_.sceneVersion) + ")");

        if (sqliteScenes) {
            const fs::path dbPath = projectSqlitePath(projectManager_.manifest());
            SceneRepositorySqlite repo(dbPath.string());
            std::string sqliteError;
            if (repo.saveScene(fileName, scene_, &sqliteError)) {
                addLog("[SCENE] Synced JSON scene into SQLite: " + fileName);
                refreshSceneFiles();
            } else {
                addLog("[SCENE] SQLite sync after JSON load failed: " + sqliteError);
            }
        }
    } else {
        addLog("ERROR: Could not load scene: " + path);
        for (auto& err : scene_.loadErrors)
            addLog("  [load] " + err);
    }
}

void EditorApp::applySceneToWorld()
{
    for (auto& ovr : scene_.tileOverrides) {
        if (ovr.x >= 0 && ovr.x < WORLD_W && ovr.y >= 0 && ovr.y < WORLD_H) {
            // Apply to terrain mesh face
            TerrainFace& f = world_.terrain().face(ovr.x, ovr.y);
            f.type     = static_cast<TileType>(ovr.tileType);
            f.walkable = ovr.walkable;

            // Keep legacy grid in sync
            world_.grid[ovr.y][ovr.x].type     = static_cast<TileType>(ovr.tileType);
            world_.grid[ovr.y][ovr.x].walkable  = ovr.walkable;
        }
    }

    // Apply vertex height overrides
    for (auto& vh : scene_.vertexHeightOverrides) {
        if (vh.vx >= 0 && vh.vx < TerrainMesh::VW &&
            vh.vy >= 0 && vh.vy < TerrainMesh::VH) {
            world_.terrain().vert(vh.vx, vh.vy).height = vh.height;
        }
    }

    // Apply cliff overrides (v5+)
    for (auto& co : scene_.cliffOverrides) {
        if (co.vx >= 0 && co.vx < TerrainMesh::VW &&
            co.vy >= 0 && co.vy < TerrainMesh::VH) {
            world_.terrain().setCliffLevel(co.vx, co.vy, co.cliffLevel);
        }
    }

    // Apply texture overrides (v5+)
    for (auto& to : scene_.textureOverrides) {
        if (to.vx >= 0 && to.vx < TerrainMesh::VW &&
            to.vy >= 0 && to.vy < TerrainMesh::VH) {
            auto& v = world_.terrain().vert(to.vx, to.vy);
            std::memcpy(v.texIndices, to.texIndices, 4);
            std::memcpy(v.texWeights, to.texWeights, 4);
        }
    }

    // Apply water bodies (v5+)
    auto& wbs = world_.terrain().waterBodies();
    wbs.clear();
    for (auto& wb : scene_.waterBodies) {
        world_.terrain().addWaterBody(wb);
    }

    world_.terrain().markDirty();
}

// ═════════════════════════════════════════════════════════════════════════════
// Unsaved-changes guard
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::requestAction(PendingAction action)
{
    if (scene_.modified) {
        pendingAction_     = action;
        showConfirmDialog_ = true;
    } else {
        pendingAction_ = action;
        executePendingAction();
    }
}

void EditorApp::executePendingAction()
{
    PendingAction action = pendingAction_;
    pendingAction_     = PendingAction::None;
    showConfirmDialog_ = false;

    switch (action) {
    case PendingAction::NewScene:
        newScene();
        break;
    case PendingAction::OpenScene:
        refreshSceneFiles();
        showOpenDialog_ = true;
        break;
    case PendingAction::Exit:
        running_ = false;
        break;
    case PendingAction::None:
        break;
    }
}

void EditorApp::drawConfirmDialog()
{
    ImGui::OpenPopup("Unsaved Changes");
    if (ImGui::BeginPopupModal("Unsaved Changes", &showConfirmDialog_,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("The current scene has unsaved changes.");
        ImGui::Text("Do you want to save before continuing?");
        ImGui::Separator();

        if (ImGui::Button("Save", {100, 0})) {
            if (scene_.filePath.empty())
                showSaveDialog_ = true;
            else
                saveScene(scene_.filePath);
            executePendingAction();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", {100, 0})) {
            executePendingAction();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {100, 0})) {
            pendingAction_     = PendingAction::None;
            showConfirmDialog_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Play Mode – snapshot & rollback
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::enterPlayMode()
{
    if (editorMode_ == EditorMode::Play) return;

    beginPlayAuditSession();

    addLog("[VSTEP] enterPlayMode begin");
    addLog("[VSTEP] Current scene name: " + scene_.sceneName);
    addLog("[VSTEP] Current scene entities: " + std::to_string(scene_.entities.size()));
    for (size_t i = 0; i < scene_.entities.size(); ++i) {
        const auto& e = scene_.entities[i];
        addLog("[VSTEP]   Entity[" + std::to_string(i) + "]: " + e.name + " (type=" + 
               (e.type == EntityData::Type::Player ? "Player" : "Enemy") + ")");
    }

    playSession_.capture(scene_, world_);

    // Export current scene to temp file for Vulkan to load
    std::string tempScene = std::string(BUILD_DIR) + "/_play_scene.json";
    std::string prevPath = scene_.filePath;
    bool prevMod = scene_.modified;
    
    const bool saved = scene_.saveToFile(tempScene);
    
    // Append tilemap (world grid) to the exported scene JSON
    if (saved) {
        std::ifstream in(tempScene);
        json j;
        if (in >> j) {
            in.close();
            // Export the entire world.grid as a flat tilemap array
            // tilemap[y*WORLD_W + x] = tileType (0-8)
            std::vector<int> tilemap;
            for (int y = 0; y < WORLD_H; ++y) {
                for (int x = 0; x < WORLD_W; ++x) {
                    tilemap.push_back(static_cast<int>(world_.grid[y][x].type));
                }
            }
            j["tilemap"] = tilemap;
            j["worldWidth"] = WORLD_W;
            j["worldHeight"] = WORLD_H;
            
            std::ofstream out(tempScene);
            out << j.dump(2);
            out.close();
        }
    }
    
    scene_.filePath = prevPath;
    scene_.modified = prevMod;
    addLog(std::string("[Play] Scene exported: ") + (saved ? "ok" : "failed"));

    editorMode_ = EditorMode::Play;

    // Never inherit pause/speed from a previous session.
    playback_.reset();
    syncPlaybackStateFile(true);

    // Center camera on player entity
    for (const auto& e : scene_.entities) {
        if (e.type == EntityData::Type::Player) {
            camX_ = e.x;
            camY_ = e.y;
            break;
        }
    }

    addLog("Entered Play mode.");
}

void EditorApp::exitPlayMode()
{
    if (editorMode_ != EditorMode::Play) return;

    flushPlayAuditSessionToFile("play_stopped");

    playSession_.restore(scene_, world_);
    clearSelection();
    editorMode_ = EditorMode::Edit;
    playback_.reset();
    syncPlaybackStateFile(true);
    addLog("Exited Play mode (scene restored).");

    // ── Apply hot-reload changes that were deferred during Play ──────────────
    if (!deferredReloads_.empty()) {
        std::vector<std::string> reloadErrors;
        bool dbChanged = importManager_.reimportChanged(
            deferredReloads_, assetsRoot_, libraryRoot_, assetDb_, reloadErrors);
        for (const auto& ch : deferredReloads_)
            addLog("[Hot-Reload] Reimported: " + ch.relativePath);
        for (const auto& err : reloadErrors)
            addLog("[IMPORT] " + err);
        if (dbChanged) {
            assetDb_.save(assetDbPath_);
        }
        deferredReloads_.clear();
    }
}

void EditorApp::buildAndRun()
{
    addLog("[VSTEP] Build & Run begin");
    addLog("--- Building Vulkan runtime ---");
    showBuildLog_ = true;

#ifdef _WIN32
    // Locate cmake: check PATH first, then common VS install locations.
    std::string cmakeExe;
    if (std::system("where cmake >nul 2>&1") == 0) {
        cmakeExe = "cmake";
    } else {
        const char* vsCandidates[] = {
            "C:\\Program Files\\Microsoft Visual Studio\\18\\Insiders\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe",
        };
        for (const char* p : vsCandidates) {
            if (fs::exists(p)) { cmakeExe = p; break; }
        }
    }
    if (cmakeExe.empty()) {
        addLog("[VFAIL] cmake not found. Make sure Visual Studio or CMake is installed.");
        addLog("        To build VulkanBootstrap manually: dash build -Vulkan");
        return;
    }
    const std::vector<std::string> buildArgv = {
        cmakeExe, "--build", BUILD_DIR, "--target", "VulkanBootstrap", "--config", "Release"
    };
#else
    const std::vector<std::string> buildArgv = {
        "cmake", "--build", BUILD_DIR, "--target", "VulkanBootstrap", "--parallel"
    };
#endif

    const int ret = runProcessCapture(buildArgv, [this](const std::string& line) {
        addLog(line);
    });
    if (ret < 0) {
        addLog("ERROR: Could not start build.");
        return;
    }

    if (ret == 0) {
        addLog("[VOK] Build OK. Launching Vulkan runtime...");

        // Export scene + tilemap to temp file so Vulkan has full terrain data.
        std::string tempScene = std::string(BUILD_DIR) + "/_play_scene.json";
        std::string prevPath = scene_.filePath;
        bool prevMod = scene_.modified;
        if (scene_.saveToFile(tempScene)) {
            std::ifstream in(tempScene);
            json j;
            if (in >> j) {
                in.close();
                std::vector<int> tilemap;
                tilemap.reserve(WORLD_W * WORLD_H);
                for (int y = 0; y < WORLD_H; ++y) {
                    for (int x = 0; x < WORLD_W; ++x) {
                        tilemap.push_back(static_cast<int>(world_.grid[y][x].type));
                    }
                }
                j["tilemap"] = tilemap;
                j["worldWidth"] = WORLD_W;
                j["worldHeight"] = WORLD_H;
                std::ofstream out(tempScene);
                out << j.dump(2);
                out.close();
            }

            scene_.filePath = prevPath;
            scene_.modified = prevMod;
            addLog("[VOK] Scene exported to " + tempScene);
            addLog("[VSTEP] Tilemap exported (" + std::to_string(WORLD_W * WORLD_H) + " tiles)");
        } else {
            addLog("[VFAIL] Could not export scene, launching with defaults.");
            tempScene.clear();
        }

        const fs::path buildDir = fs::path(BUILD_DIR);
        std::array<fs::path, 6> candidates = {
            buildDir / "Release" / "VulkanBootstrap.exe",
            buildDir / "Release" / "VulkanBootstrap",
            buildDir / "VulkanBootstrap.exe",
            buildDir / "VulkanBootstrap",
            buildDir / "src" / "tools" / "Release" / "VulkanBootstrap.exe",
            buildDir / "Debug" / "VulkanBootstrap.exe",
        };

        fs::path executablePath;
        std::error_code ec;
        for (const auto& c : candidates) {
            if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) {
                executablePath = c;
                break;
            }
            ec.clear();
        }

        if (executablePath.empty()) {
            addLog("[VFAIL] VulkanBootstrap executable not found under: " + std::string(BUILD_DIR));
            return;
        }

        std::vector<std::string> runArgs;
        runArgs.emplace_back("--persistent");
        if (!tempScene.empty()) {
            runArgs.emplace_back("--scene");
            runArgs.push_back(tempScene);
        }
        // Transport channel: the runtime polls this file for pause/step/timeScale.
        playback_.reset();
        syncPlaybackStateFile(true);
        runArgs.emplace_back("--state");
        runArgs.push_back(playbackStatePath());

        {
            std::string argsLog = "[VSTEP] Build&Run launch args:";
            for (const auto& a : runArgs) argsLog += " " + a;
            addLog(argsLog);
        }

        std::string launchError;
        intptr_t pid = -1;
        if (!spawnTrackedProcess(executablePath, runArgs, pid, launchError)) {
            addLog("[VFAIL] Could not launch Vulkan runtime: " + launchError);
            return;
        }

        // Bring the game window to front on macOS
#ifdef __APPLE__
        {
            std::vector<std::string> osaArgs = {
                "-e", "delay 0.3",
                "-e", "tell application \"System Events\"",
                "-e", "  set frontmost of (first process whose name is \"VulkanBootstrap\") to true",
                "-e", "end tell"
            };
            intptr_t osaPid = -1;
            std::string osaError;
            spawnTrackedProcess("/usr/bin/osascript", osaArgs, osaPid, osaError);
        }
#endif
        addLog("Game launched: " + executablePath.string());

        // The launched runtime is a play session: without this the transport
        // controls stay hidden while the process that consumes them is alive.
        editorMode_ = EditorMode::Play;
    } else {
        addLog("[VFAIL] Build FAILED (exit " + std::to_string(ret) + ").");
    }
}

void EditorApp::exportGameBundle()
{
    showBuildLog_ = true;
    addLog("--- Export Game Bundle ---");

    if (!projectManager_.hasActiveProject()) {
        addLog("[ERROR] No active project. Open a .dashproject before exporting.");
        return;
    }

    // Preserve scene state and export current editor scene into project scenes dir.
    if (!scene_.filePath.empty()) {
        const std::string prevPath = scene_.filePath;
        const bool prevModified = scene_.modified;
        if (!scene_.saveToFile(scene_.filePath)) {
            addLog("[ERROR] Failed to save scene before export: " + scene_.filePath);
        }
        scene_.filePath = prevPath;
        scene_.modified = prevModified;
    }

    const auto& manifest = projectManager_.manifest();
    std::string outDir = manifest.absoluteBuildDir();
    if (outDir.empty()) outDir = AppPaths::getBuildOutputDir();

    auto result = GameBuildPipeline::build(manifest, outDir, BUILD_DIR);
    for (const auto& line : result.log)
        addLog(line);

    if (result.success)
        addLog("Export OK: " + result.outputPath);
    else
        addLog("Export FAILED.");
}
