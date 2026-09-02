// ═════════════════════════════════════════════════════════════════════════════
// EditorApp — core: init/shutdown, the main loop and the undo/redo glue.
//
// Everything else (project & scene IO, play mode, build & run, menus/dialogs,
// panels, viewport, gizmos) lives in sibling EditorXxx.cpp files split out of
// this one to keep it navigable — see each file's header comment.
// ═════════════════════════════════════════════════════════════════════════════
#include "EditorApp.h"
#include "icon_data.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"
#include "imgui_internal.h"
#include "VersionInfo.h"
#include "world/BiomeTableFile.h"
#include "Profiler.h"
#include "AppPaths.h"
#include "IconsFontAwesome6.h"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

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

    // Biome table before any world generation: an empty table makes the terrain
    // fall back to its built-in thresholds, and the viewport would then disagree
    // with what the runtime draws. scene_ is still the default-constructed scene
    // here (biomeTableId empty), so this resolves to the global biomes.json.
    loadBiomeTable();

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

    // ── Gameplay database (items/enemies/classes) ─────────────────
    if (gameplayDb_.load(assetsRoot_))
        addLog("Gameplay DB loaded (" + std::to_string(gameplayDb_.items().size()) + " items, "
              + std::to_string(gameplayDb_.enemies().size()) + " enemy types, "
              + std::to_string(gameplayDb_.playerClasses().size()) + " classes).");
    else
        addLog("[WARN] Failed to load gameplay DB.");

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

    // Split centre: viewport (top) | bottom drawer (~22%). The toolbar is not a
    // dock node any more, so the centre column starts at the viewport.
    ImGuiID dockViewport, dockBottom;
    ImGui::DockBuilderSplitNode(dockCentre, ImGuiDir_Down, 0.22f,
                                &dockBottom, &dockViewport);

    // Split left panel: scene hierarchy (top 55%) | tile palette (bottom 45%)
    ImGuiID dockHierarchy, dockPalette;
    ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Down, 0.45f,
                                &dockPalette, &dockHierarchy);

    // Split right panel: properties (top 55%) | file browser (bottom 45%)
    ImGuiID dockProperties, dockFileBrowser;
    ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.45f,
                                &dockFileBrowser, &dockProperties);

    // Dock each window into its slot. Every panel needs a home here: one that is
    // missing opens floating at an arbitrary spot the first time it is toggled.
    ImGui::DockBuilderDockWindow("Scene Hierarchy",  dockHierarchy);
    ImGui::DockBuilderDockWindow("Scene Selector",   dockHierarchy);
    ImGui::DockBuilderDockWindow("Tile Palette",     dockPalette);
    ImGui::DockBuilderDockWindow("Viewport",         dockViewport);
    ImGui::DockBuilderDockWindow("File Editor",      dockViewport);
    ImGui::DockBuilderDockWindow("Entity Viewport",  dockViewport);
    ImGui::DockBuilderDockWindow("Bone Structure",   dockViewport);
    ImGui::DockBuilderDockWindow("Biome Designer",   dockViewport);
    ImGui::DockBuilderDockWindow("Animation",        dockViewport);
    ImGui::DockBuilderDockWindow("State Machine",    dockViewport);
    ImGui::DockBuilderDockWindow("Properties",       dockProperties);
    ImGui::DockBuilderDockWindow("Asset Inspector",  dockProperties);
    ImGui::DockBuilderDockWindow("Lighting",         dockProperties);
    ImGui::DockBuilderDockWindow("Audio",            dockProperties);
    ImGui::DockBuilderDockWindow("File Browser",     dockFileBrowser);
    ImGui::DockBuilderDockWindow("Asset Browser",    dockBottom);
    ImGui::DockBuilderDockWindow("Build Log",        dockBottom);
    ImGui::DockBuilderDockWindow("Performance",      dockBottom);
    ImGui::DockBuilderDockWindow("Validation",       dockBottom);
    ImGui::DockBuilderDockWindow("Runtime Inspector", dockBottom);

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

        if (showToolbar_) drawToolbar();
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
        if (showBiomeDesignerPanel_)
            biomeDesignerPanel_.draw(biomeTable_, world_.terrain(), assetsRoot_,
                                     scene_.worldSeed, scene_.biomeTableId,
                                     [this](unsigned int){ regenerateWorld(); },
                                     [this](const std::string& m){ addLog(m); });
        if (showAnimationPanel_)
            animationPanel_.draw(scene_, selectedEntityId_, animationSets_, animators_,
                                 [this](const std::string& meshId){ return vkCtx_.resolveModelPath(meshId); },
                                 [this](const std::string& m){ addLog(m); });
        if (showStateMachinePanel_)
            stateMachinePanel_.draw(assetsRoot_, [this](const std::string& m){ addLog(m); });
        if (showItemsPanel_)
            itemsPanel_.draw(gameplayDb_, assetsRoot_, [this](const std::string& m){ addLog(m); });
        if (showBestiaryPanel_)
            bestiaryPanel_.draw(gameplayDb_, assetsRoot_, [this](const std::string& m){ addLog(m); });
        if (showClassesPanel_)
            classesPanel_.draw(gameplayDb_, assetsRoot_, [this](const std::string& m){ addLog(m); });
        if (showSettlementPanel_)
            settlementPanel_.draw(scene_, world_, commandStack_, assetsRoot_,
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

