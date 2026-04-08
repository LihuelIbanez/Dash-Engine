#include "EditorApp.h"
#include "icon_data.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_internal.h"
#include "IsoRenderer.h"
#include "VersionInfo.h"
#include "PaintTileCommand.h"
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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ═════════════════════════════════════════════════════════════════════════════
// Initialisation
// ═════════════════════════════════════════════════════════════════════════════
bool EditorApp::init()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    window_ = SDL_CreateWindow(
        "Isometric RPG Editor",
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

    // Load SF Pro (SFNS) — falls back to ImGui default if not found
    const char* sfProPath = "/System/Library/Fonts/SFNS.ttf";
    if (FILE* f = fopen(sfProPath, "rb")) {
        fclose(f);
        io.Fonts->AddFontFromFileTTF(sfProPath, 15.0f);
    } else {
        io.Fonts->AddFontDefault();
    }

    // Merge Font Awesome 6 solid icons into the same font atlas
    std::string faPath = AppPaths::getResourcesDir() + "/assets/fonts/fa-solid-900.ttf";
    if (FILE* fa = fopen(faPath.c_str(), "rb")) {
        fclose(fa);
        ImFontConfig cfg;
        cfg.MergeMode        = true;
        cfg.GlyphMinAdvanceX = 13.f;   // keep icons monospace-ish
        cfg.PixelSnapH       = true;
        static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
        io.Fonts->AddFontFromFileTTF(faPath.c_str(), 13.f, &cfg, icon_ranges);
    }

    // Dark style with Unreal-like colour scheme
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding   = 2.f;
    st.FrameRounding    = 2.f;
    st.GrabRounding     = 2.f;
    st.Colors[ImGuiCol_WindowBg]       = {0.12f, 0.12f, 0.12f, 1.f};
    st.Colors[ImGuiCol_TitleBg]        = {0.08f, 0.08f, 0.08f, 1.f};
    st.Colors[ImGuiCol_TitleBgActive]  = {0.16f, 0.16f, 0.16f, 1.f};
    st.Colors[ImGuiCol_Header]         = {0.20f, 0.20f, 0.20f, 1.f};
    st.Colors[ImGuiCol_HeaderHovered]  = {0.28f, 0.28f, 0.28f, 1.f};
    st.Colors[ImGuiCol_Button]         = {0.22f, 0.22f, 0.22f, 1.f};
    st.Colors[ImGuiCol_ButtonHovered]  = {0.30f, 0.50f, 0.30f, 1.f};
    st.Colors[ImGuiCol_ButtonActive]   = {0.20f, 0.70f, 0.20f, 1.f};
    st.Colors[ImGuiCol_Tab]            = {0.15f, 0.15f, 0.15f, 1.f};
    st.Colors[ImGuiCol_TabHovered]     = {0.28f, 0.28f, 0.28f, 1.f};

    ImGui_ImplSDL2_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer2_Init(renderer_);

    // ── Viewport render-target texture (same size as game screen) ────────────
    viewportTex_ = SDL_CreateTexture(renderer_,
        SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        SCREEN_W, SCREEN_H);

    // ── Scenes directory ─────────────────────────────────────────────────────
    scenesDir_ = AppPaths::getResourcesDir() + "/scenes";
    fs::create_directories(scenesDir_);

    // ── File browser root ────────────────────────────────────────────────────
    fileBrowserRoot_ = AppPaths::getResourcesDir() + "/src";

    // ── Asset Database ─────────────────────────────────────────────────────
    assetDbPath_ = AppPaths::getResourcesDir() + "/assets/asset_db.json";
    if (fs::exists(assetDbPath_)) {
        if (assetDb_.load(assetDbPath_))
            addLog("Asset DB loaded (" + std::to_string(assetDb_.records().size()) + " records).");
        else
            addLog("[WARN] Failed to load asset DB.");
    } else {
        addLog("Asset DB not found, starting fresh.");
    }

    // ── Initial asset import ─────────────────────────────────────────────────
    assetsRoot_  = AppPaths::getResourcesDir() + "/assets";
    libraryRoot_ = AppPaths::getResourcesDir() + "/library";
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

    spriteEditor_.init(renderer_);
    spriteEditor_.selectedEntityId = &selectedEntityId_;
    spriteEditor_.scene            = &scene_;
    spriteEditor_.commandStack     = &commandStack_;
    spriteEditor_.world            = &world_;
    spriteEditor_.importManager    = &importManager_;
    spriteEditor_.assetsRoot       = &assetsRoot_;
    spriteEditor_.libraryRoot      = &libraryRoot_;
    newScene();
    running_ = true;
    addLog("Editor ready.");

    // ── Cursors ──────────────────────────────────────────────────────────────
    cursorArrow_     = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    cursorCrosshair_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    cursorHand_      = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    cursorMove_      = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);

    return true;
}

EditorApp::~EditorApp()
{
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

        // Full-window dockspace
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
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
        ImGui::End();

        // Panels — always drawn so they can be docked
        drawToolbar();
        drawSceneHierarchy();
        drawPropertiesPanel();
        drawTilePalette();
        drawViewport();
        drawBuildLog();
        drawPerformancePanel();
        drawFileBrowser();
        drawFileEditor();
        assetBrowserPanel_.draw(assetDb_, importManager_, assetsRoot_,
                                libraryRoot_, assetDbPath_,
                                [this](const std::string& m){ addLog(m); });
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
        if (showConfirmDialog_) drawConfirmDialog();

        // Update window title with dirty indicator and mode
        {
            std::string title = "Isometric RPG Editor - " + scene_.sceneName;
            if (scene_.modified) title += " *";
            if (editorMode_ == EditorMode::Play) title += "  [PLAYING]";
            SDL_SetWindowTitle(window_, title.c_str());
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
        if (ImGui::MenuItem(ICON_FA_DOOR_OPEN " Exit"))
            requestAction(PendingAction::Exit);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Build Log", nullptr, &showBuildLog_);
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
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem(ICON_FA_CIRCLE_INFO " About DashEngine"))
            showAboutModal_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawToolbar()
{
    ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoScrollbar);

    // ▶ Build & Run (green button)
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.10f, 0.50f, 0.10f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  {0.20f, 0.70f, 0.20f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   {0.10f, 0.90f, 0.10f, 1.f});
    if (ImGui::Button(ICON_FA_HAMMER "  Build & Run  ", {170, 34})) buildAndRun();
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // ▶ Play / ■ Stop (in-editor play mode)
    if (editorMode_ == EditorMode::Edit) {
        ImGui::PushStyleColor(ImGuiCol_Button,       {0.10f, 0.35f, 0.60f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.15f, 0.50f, 0.80f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.10f, 0.60f, 0.90f, 1.f});
        if (ImGui::Button(ICON_FA_PLAY "  Play  ", {110, 34})) enterPlayMode();
        ImGui::PopStyleColor(3);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,       {0.60f, 0.15f, 0.10f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.80f, 0.25f, 0.15f, 1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.90f, 0.30f, 0.15f, 1.f});
        if (ImGui::Button(ICON_FA_STOP "  Stop  ", {110, 34})) exitPlayMode();
        ImGui::PopStyleColor(3);
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Tool buttons (disabled in Play mode)
    bool inEdit = (editorMode_ == EditorMode::Edit);
    auto toolBtn = [&](const char* label, Tool t) {
        bool sel = (currentTool_ == t);
        if (!inEdit) ImGui::BeginDisabled();
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, {0.25f, 0.45f, 0.75f, 1.f});
        if (ImGui::Button(label, {110, 34})) currentTool_ = t;
        if (sel) ImGui::PopStyleColor();
        if (!inEdit) ImGui::EndDisabled();
        ImGui::SameLine();
    };

    toolBtn(ICON_FA_ARROW_POINTER " Select",      Tool::Select);
    toolBtn(ICON_FA_PAINTBRUSH    " Paint Tile",   Tool::PaintTile);
    toolBtn(ICON_FA_SKULL         " Place Enemy",  Tool::PlaceEnemy);
    toolBtn(ICON_FA_ERASER        " Erase",        Tool::Erase);

    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // ✔ Validate Scene
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.35f, 0.20f, 0.55f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  {0.50f, 0.30f, 0.75f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   {0.60f, 0.40f, 0.85f, 1.f});
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

    struct TileInfo { const char* name; TileType type; ImVec4 col; };
    TileInfo tiles[] = {
        {"Deep Water", TileType::DeepWater, {0.04f, 0.07f, 0.22f, 1.f}},
        {"Water",      TileType::Water,     {0.08f, 0.14f, 0.31f, 1.f}},
        {"Sand",       TileType::Sand,      {0.43f, 0.35f, 0.20f, 1.f}},
        {"Grass",      TileType::Grass,     {0.14f, 0.22f, 0.10f, 1.f}},
        {"Forest",     TileType::Forest,    {0.08f, 0.16f, 0.06f, 1.f}},
        {"Dirt",       TileType::Dirt,      {0.25f, 0.16f, 0.10f, 1.f}},
        {"Stone",      TileType::Stone,     {0.27f, 0.25f, 0.24f, 1.f}},
        {"Mountain",   TileType::Mountain,  {0.22f, 0.20f, 0.19f, 1.f}},
        {"Snow",       TileType::Snow,      {0.63f, 0.65f, 0.69f, 1.f}},
    };

    for (auto& t : tiles) {
        bool sel = (selectedTileType_ == t.type && currentTool_ == Tool::PaintTile);

        ImGui::PushStyleColor(ImGuiCol_Button, t.col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            {t.col.x + 0.12f, t.col.y + 0.12f, t.col.z + 0.12f, 1.f});

        if (sel) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
            ImGui::PushStyleColor(ImGuiCol_Border, {1.f, 1.f, 0.f, 1.f});
        }

        if (ImGui::Button(t.name, {ImGui::GetContentRegionAvail().x, 32})) {
            selectedTileType_ = t.type;
            currentTool_      = Tool::PaintTile;
        }

        if (sel) {
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
        ImGui::PopStyleColor(2);
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

    // Render the world to texture
    renderWorldToTexture();

    // Display texture scaled to available space
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1) avail.x = 1;
    if (avail.y < 1) avail.y = 1;
    vpDisplayW_ = avail.x;
    vpDisplayH_ = avail.y;

    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)viewportTex_, avail);

    // ── Prefab drag-drop target ──────────────────────────────────────────────
    if (editorMode_ == EditorMode::Edit && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PREFAB_GUID")) {
            std::string guid(static_cast<const char*>(payload->Data),
                             static_cast<size_t>(payload->DataSize) - 1);
            ImGuiIO& io = ImGui::GetIO();
            float mx = io.MousePos.x - cursorPos.x;
            float my = io.MousePos.y - cursorPos.y;
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

    // ── Play-mode input: forward clicks to the embedded game ─────────────────
    if (editorMode_ == EditorMode::Play && playGame_ && vpHovered) {
        ImGuiIO& io = ImGui::GetIO();
        float mx = io.MousePos.x - cursorPos.x;
        float my = io.MousePos.y - cursorPos.y;

        // Map viewport-relative coords to game screen coords
        int sx = static_cast<int>(mx * SCREEN_W / vpDisplayW_);
        int sy = static_cast<int>(my * SCREEN_H / vpDisplayH_);

        SDL_SetCursor(cursorHand_);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            playGame_->injectClick(sx, sy, true);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            playGame_->injectAttack();
    }

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

        float mx = io.MousePos.x - cursorPos.x;
        float my = io.MousePos.y - cursorPos.y;

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
            currentTool_ == Tool::PaintTile &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            float wx, wy;
            if (viewportScreenToWorld(mx, my, wx, wy))
                paintTileAt(wx, wy);
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
    int tx = (int)wx, ty = (int)wy;
    if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) return;
    if (world_.grid[ty][tx].type == selectedTileType_) return;

    auto cmd = std::make_unique<PaintTileCommand>(tx, ty, selectedTileType_);
    commandStack_.execute(std::move(cmd), scene_, world_);
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

// ═════════════════════════════════════════════════════════════════════════════
// Viewport rendering (render the isometric world into the texture)
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::renderWorldToTexture()
{
    SDL_SetRenderTarget(renderer_, viewportTex_);

    // ── Play mode: let the Game render into the viewport texture ─────────────
    if (editorMode_ == EditorMode::Play && playGame_) {
        ImGuiIO& io = ImGui::GetIO();
        playGame_->tickUpdate(io.DeltaTime);
        playGame_->tickRender();
        SDL_SetRenderTarget(renderer_, nullptr);
        return;
    }

    // ── Edit mode: normal editor rendering ───────────────────────────────────
    SDL_SetRenderDrawColor(renderer_, 15, 12, 10, 255);
    SDL_RenderClear(renderer_);

    // Draw world tiles
    world_.draw(renderer_, camX_, camY_);

    // Draw entity markers
    for (int i = 0; i < (int)scene_.entities.size(); ++i) {
        auto& e = scene_.entities[i];
        Vec2f s = worldToScreen(e.x, e.y, camX_, camY_);

        const int radius = 10;

        // Try sprite first via TextureCache
        bool drewSprite = false;
        for (auto& comp : e.components) {
            if (getVariantType(comp) != ComponentType::Render) continue;
            const auto& rc = std::get<RenderComponent>(comp);
            if (rc.visible && rc.sprite != "default") {
                std::string texPath = assetsRoot_ + "/sprites/" + rc.sprite + ".png";
                SDL_Texture* spriteTex = TextureCache::instance().load(renderer_, texPath);
                if (spriteTex) {
                    int tw = 0, th = 0;
                    SDL_QueryTexture(spriteTex, nullptr, nullptr, &tw, &th);
                    float pivotX = 0.5f;
                    float pivotY = 1.0f;
                    getSpritePivot(rc.sprite, pivotX, pivotY);
                    SDL_Rect dst = {
                        static_cast<int>(s.x - pivotX * tw),
                        static_cast<int>(s.y - pivotY * th),
                        tw,
                        th
                    };
                    SDL_RenderCopy(renderer_, spriteTex, nullptr, &dst);
                    drewSprite = true;
                }
            }
            break;
        }

        if (!drewSprite) {
            SDL_Color col;
            if (e.type == EntityData::Type::Player)
                col = {60, 140, 255, 255};
            else
                col = {220, 60, 60, 255};

            // Filled circle
            SDL_SetRenderDrawColor(renderer_, col.r, col.g, col.b, col.a);
            for (int dy = -radius; dy <= radius; ++dy) {
                int half = (int)std::sqrt((float)(radius * radius - dy * dy));
                SDL_RenderDrawLine(renderer_,
                    (int)s.x - half, (int)s.y + dy,
                    (int)s.x + half, (int)s.y + dy);
            }
        }

        // Selection ring
        if (selectedEntityId_ == e.id) {
            SDL_SetRenderDrawColor(renderer_, 255, 255, 0, 255);
            int r2 = radius + 3;
            for (int dy = -r2; dy <= r2; ++dy) {
                int half = (int)std::sqrt((float)(r2 * r2 - dy * dy));
                SDL_RenderDrawPoint(renderer_, (int)s.x - half, (int)s.y + dy);
                SDL_RenderDrawPoint(renderer_, (int)s.x + half, (int)s.y + dy);
            }
        }
    }

    // Subtle grid overlay when painting tiles
    if (currentTool_ == Tool::PaintTile) {
        SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 25);
        for (int ty = 0; ty < WORLD_H; ++ty) {
            for (int tx = 0; tx < WORLD_W; ++tx) {
                Vec2f s = worldToScreen(tx + 0.5f, ty + 0.5f, camX_, camY_);
                // Skip off-screen tiles
                if (s.x < -TILE_W || s.x > SCREEN_W + TILE_W) continue;
                if (s.y < -TILE_H || s.y > SCREEN_H + TILE_H) continue;
                drawDiamondOutline(renderer_, s.x, s.y, 255, 255, 255, 25);
            }
        }
    }

    SDL_SetRenderTarget(renderer_, nullptr);
}

bool EditorApp::viewportScreenToWorld(float vx, float vy,
                                       float& wx, float& wy)
{
    // Map from the ImGui displayed image size to texture coordinates
    float sx = vx * (float)SCREEN_W / vpDisplayW_;
    float sy = vy * (float)SCREEN_H / vpDisplayH_;

    float u = (sx - SCREEN_W * 0.5f) * 2.f / TILE_W;
    float v = (sy - SCREEN_H * 0.5f) * 2.f / TILE_H;

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
    for (auto& msg : log_)
        ImGui::TextWrapped("%s", msg.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
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

// ═════════════════════════════════════════════════════════════════════════════
// Actions
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::newScene()
{
    scene_.createDefault();
    world_.generate(scene_.worldSeed);
    selectedEntityId_ = 0;
    commandStack_.clear();
    camX_ = WORLD_W / 2.f;
    camY_ = WORLD_H / 2.f;
    addLog("New scene created.");
}

void EditorApp::refreshSceneFiles()
{
    sceneFiles_.clear();
    if (!fs::exists(scenesDir_)) return;
    for (auto& entry : fs::directory_iterator(scenesDir_)) {
        if (entry.path().extension() == ".json")
            sceneFiles_.push_back(entry.path().filename().string());
    }
    std::sort(sceneFiles_.begin(), sceneFiles_.end());
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

    if (scene_.saveToFile(path)) {
        addLog("Saved: " + path + " (v" + std::to_string(SceneData::kCurrentVersion) + ")");
    } else {
        addLog("ERROR: Could not write scene file: " + path);
    }
}

void EditorApp::openScene(const std::string& path)
{
    if (scene_.loadFromFile(path, assetsRoot_)) {
        // Report any warnings collected during load
        for (auto& err : scene_.loadErrors)
            addLog("  [load] " + err);

        world_.generate(scene_.worldSeed);
        applySceneToWorld();
        selectedEntityId_ = 0;
        commandStack_.clear();
        camX_ = WORLD_W / 2.f;
        camY_ = WORLD_H / 2.f;
        addLog("Loaded: " + path + " (v" + std::to_string(scene_.sceneVersion) + ")");
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
            world_.grid[ovr.y][ovr.x].type     = static_cast<TileType>(ovr.tileType);
            world_.grid[ovr.y][ovr.x].walkable  = ovr.walkable;
        }
    }
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

    playSession_.capture(scene_, world_);

    // Export current scene to temp file for the game to load
    std::string tempScene = std::string(BUILD_DIR) + "/_play_scene.json";
    std::string prevPath = scene_.filePath;
    bool prevMod = scene_.modified;
    scene_.saveToFile(tempScene);
    scene_.filePath = prevPath;
    scene_.modified = prevMod;

    // Create embedded game instance
    playGame_ = std::make_unique<Game>();
    playGame_->setSceneFile(tempScene);
    if (!playGame_->initEmbedded(renderer_)) {
        addLog("ERROR: Could not start embedded game.");
        playGame_.reset();
        playSession_.restore(scene_, world_);
        return;
    }

    editorMode_ = EditorMode::Play;
    addLog("Entered Play mode (game running in viewport).");
}

void EditorApp::exitPlayMode()
{
    if (editorMode_ != EditorMode::Play) return;

    playGame_.reset();
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
    addLog("--- Building game ---");
    showBuildLog_ = true;

    std::string cmd = "cd \"" + std::string(BUILD_DIR)
                    + "\" && /usr/bin/make IsometricRPG 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
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
    int ret = pclose(pipe);

    if (ret == 0) {
        addLog("Build OK. Launching...");

        // Save the current scene to a temp file so the game can load it
        std::string tempScene = std::string(BUILD_DIR) + "/_play_scene.json";
        // Preserve scene state — saveToFile modifies filePath and modified flag
        std::string prevPath = scene_.filePath;
        bool prevMod = scene_.modified;
        if (scene_.saveToFile(tempScene)) {
            scene_.filePath = prevPath;
            scene_.modified = prevMod;
            addLog("Scene exported to " + tempScene);
        } else {
            addLog("WARNING: Could not export scene, launching with defaults.");
            tempScene.clear();
        }

        std::string runCmd = "\"" + std::string(BUILD_DIR)
                           + "/IsometricRPG\"";
        if (!tempScene.empty())
            runCmd += " \"" + tempScene + "\"";
        runCmd += " &";
        std::system(runCmd.c_str());
        // Bring the game window to front on macOS
        std::system("osascript -e 'delay 0.3' "
                    "-e 'tell application \"System Events\"' "
                    "-e '  set frontmost of (first process whose name is \"IsometricRPG\") to true' "
                    "-e 'end tell' &");
        addLog("Game launched.");
    } else {
        addLog("Build FAILED (exit " + std::to_string(ret) + ").");
    }
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
static void drawDirectoryTree(const fs::path& dir, EditorApp* /*unused*/,
                            std::string& clickedFile)
{
    // Collect entries and sort (dirs first, then files)
    std::vector<fs::directory_entry> entries;
    for (auto& e : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied))
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
                drawDirectoryTree(entry.path(), nullptr, clickedFile);
                ImGui::TreePop();
            }
        } else {
            ImGuiTreeNodeFlags leafFlags = ImGuiTreeNodeFlags_Leaf
                                        | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                        | ImGuiTreeNodeFlags_SpanAvailWidth;
            ImGui::TreeNodeEx(name.c_str(), leafFlags);
            if (ImGui::IsItemClicked()) {
                clickedFile = entry.path().string();
            }
        }
    }
}

void EditorApp::drawFileBrowser()
{
    ImGui::Begin("File Browser");

    // Reimport All button
    if (ImGui::Button("Reimport All")) {
        std::vector<std::string> importErrors;
        int count = importManager_.importAll(assetsRoot_, libraryRoot_, assetDb_, importErrors);
        addLog("Reimported " + std::to_string(count) + " asset(s).");
        for (auto& err : importErrors)
            addLog("[IMPORT] " + err);
        if (count > 0)
            assetDb_.save(assetDbPath_);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu assets)", assetDb_.records().size());
    ImGui::Separator();

    ImGui::TextColored({0.6f,0.9f,0.6f,1.f}, "src/");
    ImGui::Separator();

    std::string clickedFile;
    if (fs::is_directory(fileBrowserRoot_))
        drawDirectoryTree(fileBrowserRoot_, this, clickedFile);

    if (!clickedFile.empty())
        openFileInEditor(clickedFile);

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
