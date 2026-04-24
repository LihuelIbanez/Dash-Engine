#include "EditorApp.h"
#include "icon_data.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
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
#include "project/ProjectDataMigrator.h"
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
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
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

    renderer_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) return false;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

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

    ImGui_ImplSDL2_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer2_Init(renderer_);

    // ── Viewport render-target texture (same size as game screen) ────────────
    viewportTex_ = SDL_CreateTexture(renderer_,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        SCREEN_W, SCREEN_H);

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
    fileBrowserRoot_ = AppPaths::getResourcesDir();
    std::strncpy(fileBrowserNavBuf_, fileBrowserRoot_.c_str(), sizeof(fileBrowserNavBuf_) - 1);

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

    entityViewportPanel_.init(renderer_, assetsRoot_);

    spriteEditor_.init(renderer_);
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

    {
        const fs::path stateDir = fs::path(BUILD_DIR) / "generated";
        std::error_code ec;
        fs::create_directories(stateDir, ec);
        vulkanViewportStatePath_ = (stateDir / "vulkan_viewport_state.json").string();
    }

    // Detect Vulkan preview executable availability.
    {
        const fs::path buildDir = fs::path(BUILD_DIR);
        const std::array<fs::path, 6> previewCandidates = {
            buildDir / "VulkanBootstrap",
            buildDir / "VulkanBootstrap.exe",
            buildDir / "src" / "tools" / "VulkanBootstrap",
            buildDir / "src" / "tools" / "Release" / "VulkanBootstrap.exe",
            buildDir / "Release" / "VulkanBootstrap.exe",
            buildDir / "Debug" / "VulkanBootstrap",
        };
        std::error_code ec;
        for (const auto& p : previewCandidates) {
            if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) {
                vulkanPreviewAvailable_ = true;
                break;
            }
            ec.clear();
        }
    }

    running_ = true;
    addLog("Editor ready.");
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
    stopVulkanPreview();

    // Persist asset database on shutdown
    if (!assetDbPath_.empty())
        assetDb_.save(assetDbPath_);

    SDL_FreeCursor(cursorArrow_);
    SDL_FreeCursor(cursorCrosshair_);
    SDL_FreeCursor(cursorHand_);
    SDL_FreeCursor(cursorMove_);

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (viewportTex_) SDL_DestroyTexture(viewportTex_);
    TextureCache::instance().clear(renderer_);
    if (renderer_)    SDL_DestroyRenderer(renderer_);
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
    while (running_) {
        pollVulkanPreviewProcess();
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
        }

        ImGui_ImplSDLRenderer2_NewFrame();
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
        if (showFileBrowser_) drawFileBrowser();
        if (showFileEditor_) drawFileEditor();
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
        if (spriteEditor_.isOpen)
            spriteEditor_.draw();
        if (showLightingPanel_) drawLightingPanel();
        if (showEntityViewport_) {
            entityViewportPanel_.isOpen = true;
            entityViewportPanel_.draw(scene_, renderer_);
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
            if (editorMode_ == EditorMode::Play) title += "  [PLAYING]";
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
            const char* modeText = (editorMode_ == EditorMode::Edit) ? "EDIT" : "PLAYING";
            dl->AddText({x, textY}, IM_COL32(255, 255, 255, 255), modeText);
            x += ImGui::CalcTextSize(modeText).x + 20.f;

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

        // Render
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer_, 30, 30, 30, 255);
        SDL_RenderClear(renderer_);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
        SDL_RenderPresent(renderer_);
        Profiler::instance().endFrame();
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
        ImGui::MenuItem("Entity Viewport", nullptr, &showEntityViewport_);
        ImGui::MenuItem("Scene Selector", nullptr, &showSceneSelector_);
        ImGui::MenuItem("Validation Panel", nullptr, &showValidationPanel_);
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
// Scene Hierarchy
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawSceneHierarchy()
{
    ImGui::Begin("Scene Hierarchy");

    ImGui::Text("Scene: %s%s", scene_.sceneName.c_str(),
                scene_.modified ? " *" : "");
    ImGui::Separator();

    for (int i = 0; i < (int)scene_.entities.size(); ++i) {
        auto& e = scene_.entities[i];
        const char* icon = (e.type == EntityData::Type::Player) ? "[P]" : "[E]";
        char label[128];
        std::snprintf(label, sizeof(label), "%s %s##%d", icon, e.name.c_str(), i);

        if (ImGui::Selectable(label, selectedEntityId_ == e.id))
            selectedEntityId_ = e.id;
    }

    ImGui::Separator();
    if (editorMode_ == EditorMode::Play) ImGui::BeginDisabled();
    if (ImGui::Button("+ Add Enemy", {-1, 0})) {
        uint64_t newId = scene_.allocateEntityId();
        auto cmd = std::make_unique<PlaceEnemyCommand>(camX_, camY_, newId, "NewEnemy");
        commandStack_.execute(std::move(cmd), scene_, world_);
        selectedEntityId_ = newId;
        addLog("Entity added.");
    }

    EntityData* sel = findEntityById(selectedEntityId_);
    if (sel && sel->type != EntityData::Type::Player) {
        if (ImGui::Button("- Remove Selected", {-1, 0})) {
            auto cmd = std::make_unique<EraseCommand>(selectedEntityId_);
            commandStack_.execute(std::move(cmd), scene_, world_);
            selectedEntityId_ = 0;
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
        ImGui::TextDisabled("3D Isometric (Vulkan migration)");
        bool changed3D = false;
        const bool prevUseVulkan3D = viewport3D_.useVulkan3D;
        const bool prevEmbedded = viewport3D_.embeddedPreview;
        changed3D |= ImGui::Checkbox("Vulkan 3D workflow", &viewport3D_.useVulkan3D);
        if (viewport3D_.useVulkan3D) {
            changed3D |= ImGui::Checkbox("Embedded Vulkan preview (experimental)", &viewport3D_.embeddedPreview);
            ImGui::TextWrapped("Current editor backend uses SDLRenderer2 + ImGui SDL renderer. Embedded Vulkan texture interop is staged in experimental mode and currently falls back to synchronized external Vulkan preview.");
        }

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

            const bool wantsEmbedded = viewport3D_.useVulkan3D && viewport3D_.embeddedPreview;
            const bool hadEmbedded = prevUseVulkan3D && prevEmbedded;
            if (wantsEmbedded && !hadEmbedded && !vulkanPreviewRunning_) {
                vulkanPreviewStartPending_ = true;
                addLog("[VSTEP] Embedded preview start queued (waiting viewport geometry).");
            }
            if (!wantsEmbedded && hadEmbedded && vulkanPreviewRunning_) {
                stopVulkanPreview();
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Vulkan Preview");
        if (!vulkanPreviewAvailable_) {
            ImGui::TextWrapped("VulkanBootstrap executable not found in build/. Build target VulkanBootstrap to enable live Vulkan preview.");
        } else {
            const char* previewStatus = "Stopped";
            if (vulkanPreviewRunning_) {
                previewStatus = "Running";
            } else if (vulkanPreviewStartPending_) {
                previewStatus = "Queued";
            }

            ImGui::Text("Status: %s", previewStatus);
            if (vulkanPreviewStartPending_) {
                ImGui::TextDisabled("Waiting for valid viewport geometry to launch embedded preview.");
            }

            if (!vulkanPreviewRunning_) {
                if (vulkanPreviewStartPending_) {
                    if (ImGui::Button("Cancel Pending Start", ImVec2(180, 0))) {
                        vulkanPreviewStartPending_ = false;
                        addLog("[VSTEP] Cancelled queued Vulkan preview start.");
                    }
                } else {
                    if (ImGui::Button("Start Vulkan Preview", ImVec2(180, 0))) {
                        if (viewport3D_.embeddedPreview) {
                            vulkanPreviewStartPending_ = true;
                            addLog("[VSTEP] Start Vulkan Preview queued (embedded mode).");
                        } else {
                            if (!startVulkanPreview()) {
                                addLog("[Vulkan] Failed to start Vulkan preview.");
                            }
                        }
                    }
                }
            } else {
                if (ImGui::Button("Stop Vulkan Preview", ImVec2(180, 0))) {
                    stopVulkanPreview();
                }
            }
        }
    }

    ImGui::Separator();

    EntityData* ep = findEntityById(selectedEntityId_);
    if (!ep) {
        ImGui::TextDisabled("Select an entity to edit.");
        ImGui::End();
        return;
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
                            commandStack_.execute(
                                std::make_unique<EditComponentFieldCommand>(
                                    e.id, ct, prop.offset, prop.type,
                                    fieldSnap, PropertyValue{nv}, prop.name),
                                scene_, world_);
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
                            commandStack_.execute(
                                std::make_unique<EditComponentFieldCommand>(
                                    e.id, ct, prop.offset, prop.type,
                                    fieldSnap, PropertyValue{nv}, prop.name),
                                scene_, world_);
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
                                    commandStack_.execute(
                                        std::make_unique<EditComponentFieldCommand>(
                                            e.id, ct, prop.offset, prop.type,
                                            PropertyValue{*sptr}, PropertyValue{item}, prop.name),
                                        scene_, world_);
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
                                commandStack_.execute(
                                    std::make_unique<EditComponentFieldCommand>(
                                        e.id, ct, prop.offset, prop.type,
                                        PropertyValue{strSnap}, PropertyValue{nv}, prop.name),
                                    scene_, world_);
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
                                commandStack_.execute(
                                    std::make_unique<EditComponentFieldCommand>(
                                        e.id, ct, prop.offset, prop.type,
                                        PropertyValue{strSnap}, PropertyValue{nv}, prop.name),
                                    scene_, world_);
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
                        commandStack_.execute(
                            std::make_unique<EditComponentFieldCommand>(
                                e.id, ct, prop.offset, prop.type,
                                PropertyValue{prev}, PropertyValue{nv}, prop.name),
                            scene_, world_);
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
                        commandStack_.execute(
                            std::make_unique<EditComponentFieldCommand>(
                                e.id, ct, prop.offset, prop.type,
                                PropertyValue{prev}, PropertyValue{nv}, prop.name),
                            scene_, world_);
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
    for (int i = 0; i <= static_cast<int>(ComponentType::AI); ++i) {
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

    // Render the world to texture (now with valid viewport coordinates)
    renderWorldToTexture();

    // Fit texture into available space preserving aspect ratio (letterbox/pillarbox)
    const float texAspect = static_cast<float>(SCREEN_W) / static_cast<float>(SCREEN_H);
    const float panelAspect = avail.x / avail.y;
    ImVec2 imgSize;
    if (panelAspect > texAspect) {
        // Panel wider than texture → pillarbox (bars on sides)
        imgSize = {avail.y * texAspect, avail.y};
    } else {
        // Panel taller than texture → letterbox (bars top/bottom)
        imgSize = {avail.x, avail.x / texAspect};
    }
    const float offsetX = (avail.x - imgSize.x) * 0.5f;
    const float offsetY = (avail.y - imgSize.y) * 0.5f;

    // Fill background with dark color for letterbox/pillarbox bars
    ImDrawList* bgDl = ImGui::GetWindowDrawList();
    bgDl->AddRectFilled(cursorPos, {cursorPos.x + avail.x, cursorPos.y + avail.y}, IM_COL32(30, 30, 30, 255));

    // Center the image and display at fixed aspect ratio
    ImGui::SetCursorPos({ImGui::GetCursorPos().x + offsetX, ImGui::GetCursorPos().y + offsetY});
    ImGui::Image((ImTextureID)viewportTex_, imgSize);

    // Update viewport mapping for mouse coordinate conversion
    vpDisplayW_ = imgSize.x;
    vpDisplayH_ = imgSize.y;
    vpScreenX_ = cursorPos.x + offsetX;
    vpScreenY_ = cursorPos.y + offsetY;

    if (viewport3D_.useVulkan3D) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 min = {vpScreenX_, vpScreenY_};
        dl->AddRectFilled({min.x + 8, min.y + 8}, {min.x + 280, min.y + 48}, IM_COL32(18, 24, 32, 180), 4.0f);
        const char* status = nullptr;
        if (vulkanPreviewRunning_ && viewport3D_.embeddedPreview) {
            status = "Vulkan embedded preview docked";
        } else if (vulkanPreviewRunning_) {
            status = "Vulkan backend active (external preview)";
        } else {
            status = "Vulkan mode ON (start preview from World Settings)";
        }
        dl->AddText({min.x + 16, min.y + 18}, IM_COL32(170, 220, 255, 255), status);
    }

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
                    selectedEntityId_ = newId;
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

    // ── Play-mode input: Vulkan handles its own input ────────────────────────
    // (clicking in viewport sends input to Vulkan process, not editor)

    // ── Edit-mode interaction ────────────────────────────────────────────────

    // WASD camera navigation (when viewport is focused, Edit mode only)
    if (vpFocused && editorMode_ == EditorMode::Edit) {
        ImGuiIO& io = ImGui::GetIO();
        float speed = 12.f * io.DeltaTime;  // world-units per second

        // In isometric view, W/S move along the diagonal
        if (ImGui::IsKeyDown(ImGuiKey_W)) { camX_ -= speed; camY_ -= speed; }
        if (ImGui::IsKeyDown(ImGuiKey_S)) { camX_ += speed; camY_ += speed; }
        if (ImGui::IsKeyDown(ImGuiKey_A)) { camX_ -= speed; camY_ += speed; }
        if (ImGui::IsKeyDown(ImGuiKey_D)) { camX_ += speed; camY_ -= speed; }
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

        // Scroll → camera pan (vertical)
        if (io.MouseWheel != 0.f) {
            camY_ -= io.MouseWheel * 0.5f;
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

    // Play-mode overlay indicator
    if (editorMode_ == EditorMode::Play) {
        ImVec2 wp = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            {wp.x + 8, wp.y + 30}, {wp.x + 120, wp.y + 56},
            IM_COL32(200, 40, 40, 200), 4.f);
        ImGui::GetWindowDrawList()->AddText(
            {wp.x + 16, wp.y + 34}, IM_COL32(255, 255, 255, 255), "PLAYING");
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
        selectedEntityId_ = newId;
        addLog("Placed enemy.");
        break;
    }

    case Tool::Select: {
        selectedEntityId_ = 0;
        float best = 2.f;
        for (auto& e : scene_.entities) {
            float dx = e.x - wx;
            float dy = e.y - wy;
            float d  = std::sqrt(dx * dx + dy * dy);
            if (d < best) { best = d; selectedEntityId_ = e.id; }
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
            selectedEntityId_ = 0;
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

bool EditorApp::startVulkanPreview()
{
    if (vulkanPreviewRunning_) {
        addLog("[VSTEP] startVulkanPreview skipped: already running pid=" + std::to_string(static_cast<int>(vulkanPreviewPid_)));
        return true;
    }

    addLog("[VSTEP] startVulkanPreview begin");

    const fs::path buildDir = fs::path(BUILD_DIR);

    std::array<fs::path, 6> candidates = {
        buildDir / "VulkanBootstrap",
        buildDir / "VulkanBootstrap.exe",
        buildDir / "src" / "tools" / "VulkanBootstrap",
        buildDir / "src" / "tools" / "Release" / "VulkanBootstrap.exe",
        buildDir / "Release" / "VulkanBootstrap.exe",
        buildDir / "Debug" / "VulkanBootstrap",
    };

    fs::path previewExe;
    std::error_code ec;
    for (const auto& c : candidates) {
        addLog("[VSTEP] checking preview candidate: " + c.string());
        if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) {
            previewExe = c;
            break;
        }
        ec.clear();
    }
    if (previewExe.empty()) {
        const fs::path outDir = AppPaths::getBuildOutputDir();
        const std::array<fs::path, 2> outCandidates = {
            outDir / "VulkanBootstrap",
            outDir / "Debug" / "VulkanBootstrap",
        };
        for (const auto& c : outCandidates) {
            addLog("[VSTEP] checking output candidate: " + c.string());
            if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) {
                previewExe = c;
                break;
            }
            ec.clear();
        }
    }

    if (previewExe.empty()) {
        addLog("[VFAIL] VulkanBootstrap executable not found.");
        vulkanPreviewAvailable_ = false;
        return false;
    }

    addLog("[VSTEP] using VulkanBootstrap executable: " + previewExe.string());

    std::string error;
    intptr_t pid = -1;
    std::vector<std::string> args;
    args.emplace_back("--editor-preview");

    const bool forceEmbeddedForPlay = (editorMode_ == EditorMode::Play);
    const bool launchEmbedded = forceEmbeddedForPlay || viewport3D_.embeddedPreview;
    if (launchEmbedded) {
        args.emplace_back("--embedded-window");
    }
    if (!vulkanScenePath_.empty()) {
        args.emplace_back("--scene");
        args.emplace_back(vulkanScenePath_);
    }

    // Play mode requires state sync for docking and camera/selection replication.
    if (forceEmbeddedForPlay && vulkanViewportStatePath_.empty()) {
        vulkanViewportStatePath_ = std::string(BUILD_DIR) + "/generated/vulkan_viewport_state.json";
        addLog("[VSTEP] created default state path for Play: " + vulkanViewportStatePath_);
    }

    if (!vulkanViewportStatePath_.empty()) {
        args.emplace_back("--state");
        args.emplace_back(vulkanViewportStatePath_);
    }

    {
        std::string argsLog = "[Vulkan] Launch args:";
        for (const auto& a : args) {
            argsLog += " " + a;
        }
        addLog(argsLog);
    }

    if (!spawnTrackedProcess(previewExe, args, pid, error)) {
        addLog("[VFAIL] spawnTrackedProcess failed: " + error);
        return false;
    }

    vulkanPreviewPid_ = pid;
    vulkanPreviewRunning_ = true;
    vulkanPreviewAvailable_ = true;
    addLog("[VOK] Preview started (pid=" + std::to_string(static_cast<int>(pid)) + ")"
           + (launchEmbedded ? " [embedded]" : " [external]") + ".");
    return true;
}

void EditorApp::stopVulkanPreview()
{
    vulkanPreviewStartPending_ = false;

    if (!vulkanPreviewRunning_ || vulkanPreviewPid_ <= 0) {
        vulkanPreviewRunning_ = false;
        vulkanPreviewPid_ = -1;
        return;
    }

#ifdef _WIN32
    HANDLE h = reinterpret_cast<HANDLE>(vulkanPreviewPid_);
    TerminateProcess(h, 0);
    WaitForSingleObject(h, 1000);
    CloseHandle(h);
#else
    kill(static_cast<pid_t>(vulkanPreviewPid_), SIGTERM);
    int stopStatus = 0;
    (void)waitpid(static_cast<pid_t>(vulkanPreviewPid_), &stopStatus, WNOHANG);
#endif

    addLog("[VSTEP] stopVulkanPreview sent SIGTERM to pid=" + std::to_string(static_cast<int>(vulkanPreviewPid_)));
    addLog("[VOK] Preview stopped.");
    vulkanPreviewRunning_ = false;
    vulkanPreviewPid_ = -1;
}

void EditorApp::pollVulkanPreviewProcess()
{
    if (!vulkanPreviewRunning_ || vulkanPreviewPid_ <= 0) return;

#ifdef _WIN32
    HANDLE h = reinterpret_cast<HANDLE>(vulkanPreviewPid_);
    DWORD exitCode = 0;
    if (GetExitCodeProcess(h, &exitCode) && exitCode != STILL_ACTIVE) {
        CloseHandle(h);
        vulkanPreviewRunning_ = false;
        vulkanPreviewPid_ = -1;
        addLog("[VFAIL] Preview process ended unexpectedly.");
    }
#else
    int status = 0;
    const pid_t res = waitpid(static_cast<pid_t>(vulkanPreviewPid_), &status, WNOHANG);
    if (res == 0) return;
    if (res == static_cast<pid_t>(vulkanPreviewPid_)) {
        vulkanPreviewRunning_ = false;
        vulkanPreviewPid_ = -1;
        addLog("[Vulkan] Preview process ended.");
    }
#endif
}

void EditorApp::writeVulkanViewportStateFile() const
{
    if (vulkanViewportStatePath_.empty()) return;

    float selectedX = 0.0f;
    float selectedY = 0.0f;
    float selectedZ = 0.0f;
    if (selectedEntityId_ != 0) {
        for (const auto& e : scene_.entities) {
            if (e.id != selectedEntityId_) continue;
            selectedX = e.x;
            selectedY = e.y;
            selectedZ = entityWorldZ(e.id);
            break;
        }
    }

    json j;
    int winX = 0;
    int winY = 0;
    if (window_) {
        SDL_GetWindowPosition(window_, &winX, &winY);
    }
    const float globalVpX = static_cast<float>(winX) + vpScreenX_;
    const float globalVpY = static_cast<float>(winY) + vpScreenY_;

    float dpiScaleX = 1.0f;
    float dpiScaleY = 1.0f;
    if (window_ && renderer_) {
        int logicalW = 0, logicalH = 0;
        int outputW = 0, outputH = 0;
        SDL_GetWindowSize(window_, &logicalW, &logicalH);
        SDL_GetRendererOutputSize(renderer_, &outputW, &outputH);
        if (logicalW > 0 && logicalH > 0 && outputW > 0 && outputH > 0) {
            dpiScaleX = static_cast<float>(outputW) / static_cast<float>(logicalW);
            dpiScaleY = static_cast<float>(outputH) / static_cast<float>(logicalH);
        }
    }

    const float globalVpXPx = globalVpX * dpiScaleX;
    const float globalVpYPx = globalVpY * dpiScaleY;
    const float vpDisplayWPx = vpDisplayW_ * dpiScaleX;
    const float vpDisplayHPx = vpDisplayH_ * dpiScaleY;

    j["scene"] = scene_.sceneName;
    j["worldSeed"] = scene_.worldSeed;
    // Editor camera is top-down X/Y on the ground plane.
    // Vulkan preview consumes X/Z on ground and Y as vertical height.
    j["camera"] = {
        {"x", camX_},
        {"y", 2.5f},
        {"z", camY_},
        {"height", 2.5f},
        {"forward", camY_},
        {"isoYawDeg", viewport3D_.isoYawDeg},
        {"isoPitchDeg", viewport3D_.isoPitchDeg},
        {"followDistance", viewport3D_.cameraDistance},
        {"followHeight", viewport3D_.cameraHeight},
        {"zoom", viewport3D_.zoom}
    };
    j["viewport"] = {
        {"width", SCREEN_W},
        {"height", SCREEN_H},
        {"heightScale", viewport3D_.heightScale},
        {"gridOpacity", viewport3D_.gridOpacity},
        {"fogEnabled", viewport3D_.fogEnabled},
        {"fogStart", viewport3D_.fogStart},
        {"fogEnd", viewport3D_.fogEnd},
        {"lightDirX", viewport3D_.lightDirX},
        {"lightDirY", viewport3D_.lightDirY},
        {"lightDirZ", viewport3D_.lightDirZ},
        {"lightColorR", viewport3D_.lightColorR},
        {"lightColorG", viewport3D_.lightColorG},
        {"lightColorB", viewport3D_.lightColorB},
        {"lightIntensity", viewport3D_.lightIntensity},
        {"ambientStrength", viewport3D_.ambientStrength},
        {"specularStrength", viewport3D_.specularStrength},
        {"specularShininess", viewport3D_.specularShininess},
        {"screenX", globalVpX},
        {"screenY", globalVpY},
        {"screenW", vpDisplayW_},
        {"screenH", vpDisplayH_},
        {"screenXPx", globalVpXPx},
        {"screenYPx", globalVpYPx},
        {"screenWPx", vpDisplayWPx},
        {"screenHPx", vpDisplayHPx}
    };
    j["selection"] = {
        {"entityId", selectedEntityId_},
        {"x", selectedX},
        {"y", selectedY},
        {"z", selectedZ}
    };

    std::error_code ec;
    const fs::path statePath(vulkanViewportStatePath_);
    const fs::path parent = statePath.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, ec);
        ec.clear();
    }

    const fs::path tmpPath = statePath.string() + ".tmp";
    {
        std::ofstream out(tmpPath);
        if (!out.is_open()) {
            SDL_Log("[VFAIL] Could not write temp state file: %s", tmpPath.string().c_str());
            return;
        }
        out << j.dump(2);
    }

    fs::rename(tmpPath, statePath, ec);
    if (ec) {
        fs::remove(statePath, ec);
        ec.clear();
        fs::rename(tmpPath, statePath, ec);
        if (ec) {
            SDL_Log("[VFAIL] Could not replace state file: %s", vulkanViewportStatePath_.c_str());
            fs::remove(tmpPath, ec);
            return;
        }
    }

    static int s_stateWriteCounter = 0;
    ++s_stateWriteCounter;
    if ((s_stateWriteCounter % 120) == 1) {
        SDL_Log("[VSTEP] state write ok path=%s vp=(%.1f,%.1f %.1fx%.1f) px=(%.1f,%.1f %.1fx%.1f)",
                vulkanViewportStatePath_.c_str(),
                globalVpX, globalVpY, vpDisplayW_, vpDisplayH_,
                globalVpXPx, globalVpYPx, vpDisplayWPx, vpDisplayHPx);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Viewport rendering (migrated to 3D isometric workflow)
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::renderWorldToTexture()
{
    SDL_SetRenderTarget(renderer_, viewportTex_);

    // Start queued embedded preview only after viewport geometry is available.
    if (vulkanPreviewStartPending_ && !vulkanPreviewRunning_ && viewport3D_.embeddedPreview) {
        if (vpDisplayW_ > 0 && vpDisplayH_ > 0) {
            writeVulkanViewportStateFile();
            if (!startVulkanPreview()) {
                addLog("[Vulkan] Failed to start Vulkan preview (queued start).");
            }
            vulkanPreviewStartPending_ = false;
        }
    }

    // ── Play mode: Start Vulkan when viewport is ready, then sync state ──────────
    if (editorMode_ == EditorMode::Play) {
        // Start Vulkan preview on first frame (viewport coords are now valid).
        // vulkanPreviewStartFailed_ prevents retrying every frame after a failure.
        if (!vulkanPreviewRunning_ && !vulkanPreviewStartFailed_ && vpDisplayW_ > 0 && vpDisplayH_ > 0) {
            writeVulkanViewportStateFile();
            if (!startVulkanPreview()) {
                addLog("ERROR: Could not start Vulkan preview.");
                vulkanPreviewStartFailed_ = true;  // suppress further per-frame retries
                // Fallback: keep rendering editor viewport content if Vulkan startup fails.
            }
        }
        
        // Sync state every frame
        if (vulkanPreviewRunning_) {
            writeVulkanViewportStateFile();
            pollVulkanPreviewProcess();

            // Clear viewport while Vulkan renders in embedded window
            SDL_SetRenderDrawColor(renderer_, 40, 55, 75, 255);
            SDL_RenderClear(renderer_);
            SDL_SetRenderTarget(renderer_, nullptr);
            return;
        }

        // If Vulkan is not running yet, render normal viewport content as fallback.
    }

    // ── Edit mode: 3D isometric rendering ───────────────────────────────────
    SDL_SetRenderDrawColor(renderer_, 40, 55, 75, 255);  // dark sky background
    SDL_RenderClear(renderer_);

    const float zoom = std::max(0.1f, viewport3D_.zoom);
    const float hw = (TILE_W * 0.5f) * zoom;
    const float hh = (TILE_H * 0.5f) * zoom;

    // ── Terrain mesh rendering (polygon triangles) ────────────────────────────
    // Disable alpha blending for opaque terrain geometry
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    {
        const TerrainMesh& tm = world_.terrain();

        // Lambda: project a terrain vertex to screen with zoom + height
        auto projectVert = [&](int vx, int vy) -> SDL_FPoint {
            float h = tm.worldHeight(vx, vy);
            float rx = (static_cast<float>(vx) - camX_) * TILE_SCALE;
            float ry = (static_cast<float>(vy) - camY_) * TILE_SCALE;
            float sx = (rx - ry) * hw + SCREEN_W * 0.5f;
            float sy = (rx + ry) * hh - (h * TILE_SCALE * (viewport3D_.heightScale / 8.0f) * zoom) + SCREEN_H * 0.5f;
            return {sx, sy};
        };

        // Per-vertex lit color: Lambertian + biome blending + slope tint + AO
        auto shadedColor = [&](int vx, int vy) -> SDL_Color {
            // Blend biome colors from up to 4 adjacent faces
            float r = 0, g = 0, b = 0;
            int cnt = 0;
            for (int dy = -1; dy <= 0; ++dy) {
                for (int dx = -1; dx <= 0; ++dx) {
                    int ffx = vx + dx, ffy = vy + dy;
                    if (ffx >= 0 && ffx < WORLD_W && ffy >= 0 && ffy < WORLD_H) {
                        SDL_Color c = tm.face(ffx, ffy).topColor();
                        r += c.r; g += c.g; b += c.b;
                        ++cnt;
                    }
                }
            }
            if (cnt > 0) { r /= cnt; g /= cnt; b /= cnt; }

            const auto& v = tm.vert(vx, vy);

            // Slope-based rock tint
            float slopeFactor = 1.0f - v.ny;
            float rockBlend = std::max(0.0f, std::min(1.0f, slopeFactor * 3.0f - 0.3f));
            r = r * (1.0f - rockBlend) + 115.0f * rockBlend;
            g = g * (1.0f - rockBlend) + 102.0f * rockBlend;
            b = b * (1.0f - rockBlend) +  89.0f * rockBlend;

            // Directional light (from Lighting panel)
            float lx = viewport3D_.lightDirX, ly = viewport3D_.lightDirY, lz = viewport3D_.lightDirZ;
            float llen = std::sqrt(lx*lx + ly*ly + lz*lz);
            if (llen > 1e-4f) { lx /= llen; ly /= llen; lz /= llen; }
            float NdotL = std::max(0.0f, v.nx * lx + v.ny * ly + v.nz * lz);
            float amb = viewport3D_.ambientStrength;
            float diffuse = NdotL * viewport3D_.lightIntensity;
            float light = amb + diffuse;

            // Apply light color tint, AO (softened) and lighting
            float ao = 0.5f + 0.5f * v.ao;  // soften AO so valleys aren't pitch black
            float lr = viewport3D_.lightColorR, lg = viewport3D_.lightColorG, lb = viewport3D_.lightColorB;
            auto cl = [](float v) -> Uint8 {
                return static_cast<Uint8>(std::max(0.0f, std::min(255.0f, v)));
            };
            return { cl(r * light * ao * lr), cl(g * light * ao * lg), cl(b * light * ao * lb), 255 };
        };

        // Painter's order: iterate by depth (row + col)
        for (int depth = 0; depth < WORLD_W + WORLD_H - 1; ++depth) {
            int colStart = std::max(0, depth - (WORLD_H - 1));
            int colEnd   = std::min(WORLD_W - 1, depth);
            for (int col = colStart; col <= colEnd; ++col) {
                int row = depth - col;
                if (row < 0 || row >= WORLD_H) continue;

                // Frustum cull (coarse) — use cliff-aware world height
                float avgH = (tm.worldHeight(col, row) + tm.worldHeight(col+1, row) +
                              tm.worldHeight(col, row+1) + tm.worldHeight(col+1, row+1)) * 0.25f;
                Vec2f center = worldToScreenIso3D(col + 0.5f, row + 0.5f, avgH);
                if (center.x < -TILE_W * zoom || center.x > SCREEN_W + TILE_W * zoom) continue;
                if (center.y < -TILE_H * zoom * 3 || center.y > SCREEN_H + TILE_H * zoom * 3) continue;

                // 4 corner vertices with per-vertex lighting
                SDL_FPoint pTL = projectVert(col,     row);
                SDL_FPoint pTR = projectVert(col + 1, row);
                SDL_FPoint pBL = projectVert(col,     row + 1);
                SDL_FPoint pBR = projectVert(col + 1, row + 1);

                SDL_Color cTL = shadedColor(col,     row);
                SDL_Color cTR = shadedColor(col + 1, row);
                SDL_Color cBL = shadedColor(col,     row + 1);
                SDL_Color cBR = shadedColor(col + 1, row + 1);

                // Triangle 1: TL-TR-BL
                SDL_Vertex tri1[3] = {
                    {pTL, cTL, {0, 0}},
                    {pTR, cTR, {0, 0}},
                    {pBL, cBL, {0, 0}},
                };
                SDL_RenderGeometry(renderer_, nullptr, tri1, 3, nullptr, 0);

                // Triangle 2: TR-BR-BL
                SDL_Vertex tri2[3] = {
                    {pTR, cTR, {0, 0}},
                    {pBR, cBR, {0, 0}},
                    {pBL, cBL, {0, 0}},
                };
                SDL_RenderGeometry(renderer_, nullptr, tri2, 3, nullptr, 0);
            }
        }
    }
    // Restore blend mode for entity rendering (sprites need alpha blending)
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // Draw entity markers as 3D proxies (mesh-first workflow)
    for (int i = 0; i < (int)scene_.entities.size(); ++i) {
        auto& e = scene_.entities[i];

        float wz = 0.0f;
        int renderMode = static_cast<int>(RenderMode::Mesh3D);
        std::string spriteName;
        bool visible = true;
        for (auto& comp : e.components) {
            if (auto* tf = std::get_if<TransformComponent>(&comp)) {
                wz = tf->z;
            }
            if (auto* rc = std::get_if<RenderComponent>(&comp)) {
                renderMode = rc->renderMode;
                spriteName = rc->sprite;
                visible = rc->visible;
            }
        }
        if (!visible) continue;

        const Vec2f s = worldToScreenIso3D(e.x, e.y, wz);
        const float bodyHeight = 22.0f * zoom;

        if (renderMode == static_cast<int>(RenderMode::BillboardSprite) && !spriteName.empty() && spriteName != "default") {
            std::string texPath = assetsRoot_ + "/sprites/" + spriteName + ".png";
            SDL_Texture* spriteTex = TextureCache::instance().load(renderer_, texPath);
            if (spriteTex) {
                int tw = 0, th = 0;
                SDL_QueryTexture(spriteTex, nullptr, nullptr, &tw, &th);
                float pivotX = 0.5f;
                float pivotY = 1.0f;
                getSpritePivot(spriteName, pivotX, pivotY);
                SDL_Rect dst = {
                    static_cast<int>(s.x - pivotX * tw * zoom),
                    static_cast<int>(s.y - pivotY * th * zoom),
                    static_cast<int>(tw * zoom),
                    static_cast<int>(th * zoom)
                };
                SDL_RenderCopy(renderer_, spriteTex, nullptr, &dst);
            }
        } else {
            SDL_Color top = (e.type == EntityData::Type::Player)
                ? SDL_Color{70, 145, 255, 255}
                : SDL_Color{220, 80, 80, 255};
            drawIsoColumn(renderer_, s.x, s.y, hw * 0.40f, hh * 0.40f, bodyHeight, top);
        }

        if (selectedEntityId_ == e.id) {
            const Uint8 alpha = static_cast<Uint8>(std::max(20.0f, std::min(255.0f, viewport3D_.gridOpacity * 255.0f)));
            drawDiamondOutline(renderer_, s.x, s.y + hh * 0.25f, 255, 220, 65, alpha);
        }
    }

    if (currentTool_ == Tool::PaintTile || currentTool_ == Tool::FillTile ||
        currentTool_ == Tool::HeightBrush) {
        const Uint8 alpha = static_cast<Uint8>(std::max(0.0f, std::min(255.0f, viewport3D_.gridOpacity * 255.0f)));
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, alpha);
        const TerrainMesh& tm = world_.terrain();
        for (int ty = 0; ty < WORLD_H; ++ty) {
            for (int tx = 0; tx < WORLD_W; ++tx) {
                const float h = (tm.worldHeight(tx, ty) + tm.worldHeight(tx+1, ty) +
                                 tm.worldHeight(tx, ty+1) + tm.worldHeight(tx+1, ty+1)) * 0.25f;
                const Vec2f s = worldToScreenIso3D(tx + 0.5f, ty + 0.5f, h);
                if (s.x < -TILE_W || s.x > SCREEN_W + TILE_W) continue;
                if (s.y < -TILE_H || s.y > SCREEN_H + TILE_H) continue;
                drawDiamondOutline(renderer_, s.x, s.y, 255, 255, 255, alpha);
            }
        }
    }

    SDL_SetRenderTarget(renderer_, nullptr);
}

bool EditorApp::viewportScreenToWorld(float vx, float vy, float& wx, float& wy)
{
    const float sx = vx * static_cast<float>(SCREEN_W) / vpDisplayW_;
    const float sy = vy * static_cast<float>(SCREEN_H) / vpDisplayH_;

    const float zoom = std::max(0.1f, viewport3D_.zoom);
    const float hw = (TILE_W * 0.5f) * zoom * TILE_SCALE;
    const float hh = (TILE_H * 0.5f) * zoom * TILE_SCALE;

    const float u = (sx - SCREEN_W * 0.5f) / hw;
    const float v = (sy - SCREEN_H * 0.5f) / hh;

    wx = (u + v) * 0.5f + camX_;
    wy = (v - u) * 0.5f + camY_;
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

    ImGui::SeparatorText("Ambient & Specular");

    ImGui::Text("Ambient");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::SliderFloat("##ambient", &viewport3D_.ambientStrength, 0.0f, 1.0f, "%.2f");

    ImGui::Text("Specular");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::SliderFloat("##specstr", &viewport3D_.specularStrength, 0.0f, 1.0f, "%.2f");

    ImGui::Text("Shininess");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::SliderFloat("##specshin", &viewport3D_.specularShininess, 1.0f, 128.0f, "%.0f");

    ImGui::Separator();
    if (ImGui::Button("Reset Defaults")) {
        viewport3D_.lightDirX = 0.3f;  viewport3D_.lightDirY = 0.9f;  viewport3D_.lightDirZ = 0.2f;
        viewport3D_.lightColorR = 1.0f; viewport3D_.lightColorG = 0.98f; viewport3D_.lightColorB = 0.92f;
        viewport3D_.lightIntensity = 1.3f;
        viewport3D_.ambientStrength = 0.55f;
        viewport3D_.specularStrength = 0.15f;
        viewport3D_.specularShininess = 32.0f;
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
    selectedEntityId_ = 0;
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
                selectedEntityId_ = 0;
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
        selectedEntityId_ = 0;
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

    // If a preview is already running (possibly external), restart it for Play embedding.
    if (vulkanPreviewRunning_) {
        stopVulkanPreview();
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
    vulkanScenePath_ = tempScene;
    addLog(std::string("[VSTEP] play scene export ") + (saved ? "ok: " : "failed: ") + tempScene);
    addLog("[VSTEP] Tilemap exported (" + std::to_string(WORLD_W * WORLD_H) + " tiles)");

    // Enable embedded Vulkan preview mode (renders in viewport)
    viewport3D_.embeddedPreview = true;  // Force embedded window
    viewport3D_.useVulkan3D = true;
    syncSceneRender3DSettingsFromUI();
    
    vulkanViewportStatePath_ = std::string(BUILD_DIR) + "/generated/vulkan_viewport_state.json";
    
    // Vulkan will be started in renderWorldToTexture() after viewport has valid coordinates
    
    editorMode_ = EditorMode::Play;
    addLog("[VOK] Entered Play mode (Vulkan 3D starting...).");
}

void EditorApp::exitPlayMode()
{
    if (editorMode_ != EditorMode::Play) return;

    flushPlayAuditSessionToFile("play_stopped");

    stopVulkanPreview();
    vulkanScenePath_.clear();
    vulkanPreviewStartFailed_ = false;  // allow retrying on next Play press
    viewport3D_.embeddedPreview = false;  // Disable embedded mode
    viewport3D_.useVulkan3D = false;
    
    playSession_.restore(scene_, world_);
    selectedEntityId_ = 0;
    editorMode_ = EditorMode::Edit;
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
            if (fs::exists(p)) { cmakeExe = "\"" + std::string(p) + "\""; break; }
        }
    }
    if (cmakeExe.empty()) {
        addLog("[VFAIL] cmake not found. Make sure Visual Studio or CMake is installed.");
        addLog("        To build VulkanBootstrap manually: dash build -Vulkan");
        return;
    }
    std::string cmd = cmakeExe + " --build \"" + std::string(BUILD_DIR)
                    + "\" --target VulkanBootstrap --config Release 2>&1";
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    std::string cmd = "cd \"" + std::string(BUILD_DIR)
                    + "\" && make VulkanBootstrap 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) {
        addLog("ERROR: Could not start build.");
        return;
    }

    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);
        if (!line.empty() && line.back() == '\n') line.pop_back();
        if (!line.empty()) addLog(line);
    }
#ifdef _WIN32
    int ret = _pclose(pipe);
#else
    int ret = pclose(pipe);
#endif

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
        std::system("osascript -e 'delay 0.3' "
                    "-e 'tell application \"System Events\"' "
                    "-e '  set frontmost of (first process whose name is \"VulkanBootstrap\") to true' "
                    "-e 'end tell' &");
#endif
        addLog("Game launched: " + executablePath.string());
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

// ═════════════════════════════════════════════════════════════════════════════
// File helpers
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::openFileInEditor(const std::string& path)
{
    // If already open, just focus it
    for (int i = 0; i < (int)openFiles_.size(); ++i) {
        if (openFiles_[i].path == path) { activeFileTab_ = i; return; }
    }
    // Read file content
    std::ifstream ifs(path);
    if (!ifs.is_open()) { addLog("Cannot open: " + path); return; }
    std::ostringstream ss;
    ss << ifs.rdbuf();

    OpenFile f;
    f.path         = path;
    f.content      = ss.str();
    f.modified     = false;
    f.lastSnapshot = f.content;
    openFiles_.push_back(std::move(f));
    activeFileTab_ = (int)openFiles_.size() - 1;
    addLog("Opened: " + path);
}

void EditorApp::saveOpenFile(int idx)
{
    if (idx < 0 || idx >= (int)openFiles_.size()) return;
    auto& f = openFiles_[idx];
    std::ofstream ofs(f.path);
    if (!ofs.is_open()) { addLog("Cannot save: " + f.path); return; }
    ofs << f.content;
    f.modified = false;
    addLog("Saved: " + f.path);
}

void EditorApp::snapshotForUndo(OpenFile& f)
{
    if (f.content != f.lastSnapshot) {
        f.undoStack.push_back(f.lastSnapshot);
        if (f.undoStack.size() > 200) f.undoStack.erase(f.undoStack.begin());
        f.redoStack.clear();
        f.lastSnapshot = f.content;
    }
}

void EditorApp::undoFile(OpenFile& f)
{
    if (f.undoStack.empty()) return;
    f.redoStack.push_back(f.content);
    f.content      = f.undoStack.back();
    f.lastSnapshot = f.content;
    f.undoStack.pop_back();
    f.modified = true;
}

void EditorApp::redoFile(OpenFile& f)
{
    if (f.redoStack.empty()) return;
    f.undoStack.push_back(f.content);
    f.content      = f.redoStack.back();
    f.lastSnapshot = f.content;
    f.redoStack.pop_back();
    f.modified = true;
}

// ═════════════════════════════════════════════════════════════════════════════
// File Browser panel — recursive directory tree
// ═════════════════════════════════════════════════════════════════════════════
static void drawDirectoryTree(const fs::path& dir,
                              const char* filter,
                              std::string& clickedFile,
                              std::string& copiedPath,
                              const fs::path& workspaceRoot)
{
    std::string filterStr(filter ? filter : "");
    for (auto& c : filterStr)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Collect entries and sort (dirs first, then files)
    std::vector<fs::directory_entry> entries;
    std::error_code iterEc;
    for (auto& e : fs::directory_iterator(dir,
            fs::directory_options::skip_permission_denied, iterEc))
        entries.push_back(e);
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.is_directory() != b.is_directory())
            return a.is_directory() > b.is_directory();
        return a.path().filename().string() < b.path().filename().string();
    });

    for (auto& entry : entries) {
        std::string name = entry.path().filename().string();
        if (name[0] == '.') continue; // skip hidden

        if (entry.is_directory()) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                    | ImGuiTreeNodeFlags_SpanAvailWidth;
            bool open = ImGui::TreeNodeEx(name.c_str(), flags);
            if (open) {
                drawDirectoryTree(entry.path(), filter, clickedFile, copiedPath, workspaceRoot);
                ImGui::TreePop();
            }
        } else {
            // Apply filter to leaf files
            if (!filterStr.empty()) {
                std::string nameLower = name;
                for (auto& c : nameLower)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (nameLower.find(filterStr) == std::string::npos)
                    continue;
            }

            ImGuiTreeNodeFlags leafFlags = ImGuiTreeNodeFlags_Leaf
                                        | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                        | ImGuiTreeNodeFlags_SpanAvailWidth;
            ImGui::TreeNodeEx(name.c_str(), leafFlags);

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                clickedFile = entry.path().string();

            // Right-click context menu
            if (ImGui::BeginPopupContextItem()) {
                ImGui::TextDisabled("%s", name.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open in Editor"))
                    clickedFile = entry.path().string();
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_COPY "  Copy Full Path"))
                    copiedPath = entry.path().string();
                if (ImGui::MenuItem(ICON_FA_COPY "  Copy Relative Path")) {
                    std::error_code ec;
                    fs::path rel = fs::relative(entry.path(), workspaceRoot, ec);
                    copiedPath = ec ? entry.path().string() : rel.string();
                }
                ImGui::EndPopup();
            }
        }
    }
}

void EditorApp::drawFileBrowser()
{
    ImGui::Begin("File Browser");

    const std::string resDir = AppPaths::getResourcesDir();

    // ── Navigation bar ────────────────────────────────────────────────────
    ImGui::SetNextItemWidth(-52.f);
    bool navEnter = ImGui::InputText("##navpath", fileBrowserNavBuf_,
                                     sizeof(fileBrowserNavBuf_),
                                     ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    bool navGo = ImGui::SmallButton("Go");
    if (navEnter || navGo) {
        fs::path p(fileBrowserNavBuf_);
        if (fs::is_directory(p))
            fileBrowserRoot_ = p.string();
        // Sync nav buf back to resolved root
        std::strncpy(fileBrowserNavBuf_, fileBrowserRoot_.c_str(),
                     sizeof(fileBrowserNavBuf_) - 1);
    }

    // ── Bookmarks ─────────────────────────────────────────────────────────
    auto bookmark = [&](const char* icon, const char* tip, const std::string& dir) {
        if (ImGui::SmallButton(icon)) {
            if (fs::is_directory(dir)) {
                fileBrowserRoot_ = dir;
                std::strncpy(fileBrowserNavBuf_, fileBrowserRoot_.c_str(),
                             sizeof(fileBrowserNavBuf_) - 1);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
    };

    bookmark(ICON_FA_HOUSE,        resDir.c_str(),         resDir);
    bookmark(ICON_FA_IMAGE,        assetsRoot_.c_str(),    assetsRoot_);
    bookmark(ICON_FA_MAP,          scenesDir_.c_str(),     scenesDir_);
    bookmark(ICON_FA_CODE,         (resDir+"/src").c_str(), resDir + "/src");
    {
        std::string savesDir = resDir + "/saves";
        bookmark(ICON_FA_FLOPPY_DISK, savesDir.c_str(), savesDir);
    }
    // Up one level
    if (ImGui::SmallButton(ICON_FA_ARROW_UP)) {
        fs::path parent = fs::path(fileBrowserRoot_).parent_path();
        if (fs::is_directory(parent)) {
            fileBrowserRoot_ = parent.string();
            std::strncpy(fileBrowserNavBuf_, fileBrowserRoot_.c_str(),
                         sizeof(fileBrowserNavBuf_) - 1);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Up one level");

    ImGui::NewLine();
    ImGui::Separator();

    // ── Filter bar ────────────────────────────────────────────────────────
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##fbfilter", ICON_FA_MAGNIFYING_GLASS "  Filter files...",
                              fileBrowserFilter_, sizeof(fileBrowserFilter_));
    ImGui::Separator();

    // ── Directory tree ────────────────────────────────────────────────────
    std::string clickedFile, copiedPath;
    if (fs::is_directory(fileBrowserRoot_)) {
        drawDirectoryTree(fileBrowserRoot_, fileBrowserFilter_,
                          clickedFile, copiedPath, fs::path(resDir));
    } else {
        ImGui::TextColored({0.957f,0.278f,0.278f,1.f}, "Path not found: %s",
                           fileBrowserRoot_.c_str());
    }

    if (!clickedFile.empty())
        openFileInEditor(clickedFile);

    if (!copiedPath.empty()) {
        ImGui::SetClipboardText(copiedPath.c_str());
        addLog("[Browser] Copied to clipboard: " + copiedPath);
    }

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════════
// File Editor panel — tabbed text editor
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawFileEditor()
{
    ImGui::Begin("File Editor");

    if (openFiles_.empty()) {
        ImGui::TextDisabled("Open a file from the File Browser.");
        ImGui::End();
        return;
    }

    // Tab bar
    if (ImGui::BeginTabBar("##FileTabs", ImGuiTabBarFlags_Reorderable
                                        | ImGuiTabBarFlags_AutoSelectNewTabs)) {
        for (int i = 0; i < (int)openFiles_.size(); ++i) {
            auto& f = openFiles_[i];
            std::string tabLabel = fs::path(f.path).filename().string();
            if (f.modified) tabLabel += " *";
            tabLabel += "###tab" + std::to_string(i);

            bool open = true;
            ImGuiTabItemFlags tabFlags = 0;
            if (ImGui::BeginTabItem(tabLabel.c_str(), &open, tabFlags)) {
                activeFileTab_ = i;

                // Cmd+Z = undo, Cmd+Shift+Z = redo
                {
                    ImGuiIO& io = ImGui::GetIO();
                    bool cmdHeld = io.KeySuper; // Cmd on macOS
                    if (cmdHeld && ImGui::IsKeyPressed(ImGuiKey_Z)) {
                        if (io.KeyShift)
                            redoFile(f);
                        else
                            undoFile(f);
                    }
                }

                // Save button  (also Cmd+S)
                {
                    ImGuiIO& io = ImGui::GetIO();
                    if (io.KeySuper && ImGui::IsKeyPressed(ImGuiKey_S))
                        saveOpenFile(i);
                }
                if (f.modified) {
                    if (ImGui::Button("Save")) saveOpenFile(i);
                    ImGui::SameLine();
                }
                ImGui::TextDisabled("%s", f.path.c_str());

                // Text editing area
                ImVec2 avail = ImGui::GetContentRegionAvail();
                // Ensure buffer is large enough for editing
                if (f.content.capacity() < f.content.size() + 65536)
                    f.content.reserve(f.content.size() + 65536);
                f.content.resize(f.content.capacity());

                std::string editorId = "##editor" + std::to_string(i);
                if (ImGui::InputTextMultiline(editorId.c_str(),
                        f.content.data(), f.content.capacity(), avail,
                        ImGuiInputTextFlags_AllowTabInput
                        | ImGuiInputTextFlags_CallbackResize,
                        [](ImGuiInputTextCallbackData* data) -> int {
                            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                                auto* s = (std::string*)data->UserData;
                                s->resize(data->BufTextLen);
                                data->Buf = s->data();
                            }
                            return 0;
                        }, &f.content)) {
                    f.content.resize(std::strlen(f.content.c_str()));
                    snapshotForUndo(f);
                    f.modified = true;
                }

                ImGui::EndTabItem();
            }
            if (!open) {
                // Tab closed
                openFiles_.erase(openFiles_.begin() + i);
                if (activeFileTab_ >= (int)openFiles_.size())
                    activeFileTab_ = (int)openFiles_.size() - 1;
                --i;
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}
