#include "EditorApp.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "imgui_internal.h"
#include "IsoRenderer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

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

    renderer_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) return false;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // ── ImGui setup ──────────────────────────────────────────────────────────
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

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
    scenesDir_ = std::string(PROJECT_DIR) + "/scenes";
    fs::create_directories(scenesDir_);

    // ── File browser root ────────────────────────────────────────────────────
    fileBrowserRoot_ = std::string(PROJECT_DIR) + "/src";

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
    SDL_FreeCursor(cursorArrow_);
    SDL_FreeCursor(cursorCrosshair_);
    SDL_FreeCursor(cursorHand_);
    SDL_FreeCursor(cursorMove_);

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (viewportTex_) SDL_DestroyTexture(viewportTex_);
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
    ImGui::DockBuilderDockWindow("Build Log",        dockBottom);

    ImGui::DockBuilderFinish(dockspaceId);
}

// ═════════════════════════════════════════════════════════════════════════════
// Main loop
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::run()
{
    while (running_) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) running_ = false;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

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
        drawFileBrowser();
        drawFileEditor();
        if (showOpenDialog_) drawOpenDialog();
        if (showSaveDialog_) drawSaveDialog();

        // Render
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer_, 30, 30, 30, 255);
        SDL_RenderClear(renderer_);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
        SDL_RenderPresent(renderer_);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Menu bar (inside the dockspace host window)
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawMenuBar()
{
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Scene",  "Ctrl+N")) newScene();
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
            refreshSceneFiles();
            showOpenDialog_ = true;
        }
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            if (scene_.filePath.empty()) showSaveDialog_ = true;
            else saveScene(scene_.filePath);
        }
        if (ImGui::MenuItem("Save As...")) showSaveDialog_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) running_ = false;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Build Log", nullptr, &showBuildLog_);
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

// ═════════════════════════════════════════════════════════════════════════════
// Toolbar
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawToolbar()
{
    ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoScrollbar);

    // ▶ Build & Run (green button)
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.10f, 0.50f, 0.10f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  {0.20f, 0.70f, 0.20f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   {0.10f, 0.90f, 0.10f, 1.f});
    if (ImGui::Button("  Build & Run  ", {160, 34})) buildAndRun();
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    auto toolBtn = [&](const char* label, Tool t) {
        bool sel = (currentTool_ == t);
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, {0.25f, 0.45f, 0.75f, 1.f});
        if (ImGui::Button(label, {110, 34})) currentTool_ = t;
        if (sel) ImGui::PopStyleColor();
        ImGui::SameLine();
    };

    toolBtn("Select",      Tool::Select);
    toolBtn("Paint Tile",  Tool::PaintTile);
    toolBtn("Place Enemy", Tool::PlaceEnemy);
    toolBtn("Erase",       Tool::Erase);

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
    if (ImGui::Button("+ Add Enemy", {-1, 0})) {
        EntityData enemy;
        enemy.id   = scene_.allocateEntityId();
        enemy.type = EntityData::Type::Enemy;
        enemy.name = "NewEnemy";
        enemy.x    = camX_;
        enemy.y    = camY_;
        scene_.entities.push_back(enemy);
        selectedEntityId_ = enemy.id;
        scene_.modified   = true;
        addLog("Entity added.");
    }

    EntityData* sel = findEntityById(selectedEntityId_);
    if (sel && sel->type != EntityData::Type::Player) {
        if (ImGui::Button("- Remove Selected", {-1, 0})) {
            uint64_t removeId = selectedEntityId_;
            selectedEntityId_ = 0;
            scene_.entities.erase(
                std::remove_if(scene_.entities.begin(), scene_.entities.end(),
                    [removeId](const EntityData& e) { return e.id == removeId; }),
                scene_.entities.end());
            scene_.modified = true;
        }
    }

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════════
// Properties Panel
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawPropertiesPanel()
{
    ImGui::Begin("Properties");

    // World settings (always visible)
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

    if (ImGui::CollapsingHeader("Entity", ImGuiTreeNodeFlags_DefaultOpen)) {
        char name[128];
        std::strncpy(name, e.name.c_str(), sizeof(name));
        name[sizeof(name) - 1] = '\0';
        if (ImGui::InputText("Name", name, sizeof(name))) {
            e.name = name;
            scene_.modified = true;
        }

        ImGui::Text("Type: %s",
            e.type == EntityData::Type::Player ? "Player" : "Enemy");

        if (ImGui::DragFloat("X", &e.x, 0.1f, 0.f, (float)WORLD_W))
            scene_.modified = true;
        if (ImGui::DragFloat("Y", &e.y, 0.1f, 0.f, (float)WORLD_H))
            scene_.modified = true;

        if (e.type == EntityData::Type::Player) {
            const char* classes[] = {"Warrior", "Mage", "Rogue", "Archer"};
            int cur = 0;
            for (int i = 0; i < 4; ++i)
                if (e.charClass == classes[i]) { cur = i; break; }
            if (ImGui::Combo("Class", &cur, classes, 4)) {
                e.charClass = classes[cur];
                scene_.modified = true;
            }
        }
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

    // ── Interaction ──────────────────────────────────────────────────────────
    bool vpFocused = ImGui::IsWindowFocused();
    bool vpHovered = ImGui::IsItemHovered();

    // WASD camera navigation (when viewport is focused)
    if (vpFocused) {
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

        // Left-click → use current tool
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float wx, wy;
            if (viewportScreenToWorld(mx, my, wx, wy)) {
                handleToolClick(wx, wy);
            }
        }

        // Continuous painting while dragging
        if (currentTool_ == Tool::PaintTile &&
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
        EntityData enemy;
        enemy.id   = scene_.allocateEntityId();
        enemy.type = EntityData::Type::Enemy;
        enemy.name = "Enemy";
        enemy.x    = wx;
        enemy.y    = wy;
        scene_.entities.push_back(enemy);
        selectedEntityId_ = enemy.id;
        scene_.modified   = true;
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
            scene_.entities.erase(
                std::remove_if(scene_.entities.begin(), scene_.entities.end(),
                    [eraseId](const EntityData& e) { return e.id == eraseId; }),
                scene_.entities.end());
            selectedEntityId_ = 0;
            scene_.modified   = true;
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

    world_.grid[ty][tx].type     = selectedTileType_;
    bool notWalkable = (selectedTileType_ == TileType::Water ||
                        selectedTileType_ == TileType::DeepWater ||
                        selectedTileType_ == TileType::Mountain ||
                        selectedTileType_ == TileType::Snow);
    world_.grid[ty][tx].walkable = !notWalkable;

    TileOverride ovr{tx, ty, (int)selectedTileType_, world_.grid[ty][tx].walkable};
    bool found = false;
    for (auto& to : scene_.tileOverrides) {
        if (to.x == tx && to.y == ty) { to = ovr; found = true; break; }
    }
    if (!found) scene_.tileOverrides.push_back(ovr);
    scene_.modified = true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Viewport rendering (render the isometric world into the texture)
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::renderWorldToTexture()
{
    SDL_SetRenderTarget(renderer_, viewportTex_);
    SDL_SetRenderDrawColor(renderer_, 15, 12, 10, 255);
    SDL_RenderClear(renderer_);

    // Draw world tiles
    world_.draw(renderer_, camX_, camY_);

    // Draw entity markers
    for (int i = 0; i < (int)scene_.entities.size(); ++i) {
        auto& e = scene_.entities[i];
        Vec2f s = worldToScreen(e.x, e.y, camX_, camY_);

        int radius = 10;
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
    if (scene_.saveToFile(path))
        addLog("Saved: " + path);
    else
        addLog("ERROR: Could not save scene.");
}

void EditorApp::openScene(const std::string& path)
{
    if (scene_.loadFromFile(path)) {
        world_.generate(scene_.worldSeed);
        applySceneToWorld();
        selectedEntityId_ = 0;
        camX_ = WORLD_W / 2.f;
        camY_ = WORLD_H / 2.f;
        addLog("Loaded: " + path);
    } else {
        addLog("ERROR: Could not load scene.");
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
        std::string runCmd = "\"" + std::string(BUILD_DIR)
                           + "/IsometricRPG\" &";
        std::system(runCmd.c_str());
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
