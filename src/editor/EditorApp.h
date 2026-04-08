#pragma once
#include <SDL2/SDL.h>
#include "imgui.h"
#include "SceneData.h"
#include "World.h"
#include "CommandStack.h"
#include "AssetDatabase.h"
#include "ImportManager.h"
#include "AssetBrowserPanel.h"
#include "AssetInspectorPanel.h"
#include "PlaySession.h"
#include <string>
#include <vector>
#include <map>

// ─────────────────────────────────────────────────────────────────────────────
// EditorApp – Unreal-style level editor for the Isometric RPG
// ─────────────────────────────────────────────────────────────────────────────
class EditorApp {
public:
    bool init();
    void run();
    ~EditorApp();

private:
    SDL_Window*   window_      = nullptr;
    SDL_Renderer* renderer_    = nullptr;
    SDL_Texture*  viewportTex_ = nullptr;
    bool          running_     = false;

    // ── Editor mode ──────────────────────────────────────────────────────────
    enum class EditorMode { Edit, Play };
    EditorMode  editorMode_ = EditorMode::Edit;
    PlaySession playSession_;
    void enterPlayMode();
    void exitPlayMode();

    // ── Scene ────────────────────────────────────────────────────────────────
    SceneData    scene_;
    World        world_;
    uint64_t     selectedEntityId_ = 0;   // 0 = no selection
    CommandStack commandStack_;

    EntityData* findEntityById(uint64_t id);
    void performUndo();
    void performRedo();

    // ── Asset Database ────────────────────────────────────────────────────────
    AssetDatabase  assetDb_;
    std::string    assetDbPath_;
    ImportManager  importManager_;
    std::string    assetsRoot_;
    std::string    libraryRoot_;
    AssetBrowserPanel   assetBrowserPanel_;
    AssetInspectorPanel assetInspectorPanel_;

    // ── Camera ───────────────────────────────────────────────────────────────
    float camX_ = 12.f;
    float camY_ = 12.f;

    // Displayed size of the viewport image (for mouse coordinate mapping)
    float vpDisplayW_ = 1.f;
    float vpDisplayH_ = 1.f;

    // ── Tools ────────────────────────────────────────────────────────────────
    enum class Tool { Select, PaintTile, PlaceEnemy, Erase };
    Tool     currentTool_      = Tool::Select;
    TileType selectedTileType_ = TileType::Grass;

    // ── File dialogs ─────────────────────────────────────────────────────────
    std::string              scenesDir_;
    std::vector<std::string> sceneFiles_;
    bool  showOpenDialog_ = false;
    bool  showSaveDialog_ = false;
    char  saveFileName_[256] = "scene.json";

    // ── Unsaved-changes confirmation ─────────────────────────────────────────
    enum class PendingAction { None, NewScene, OpenScene, Exit };
    PendingAction pendingAction_     = PendingAction::None;
    bool          showConfirmDialog_ = false;
    void requestAction(PendingAction action);
    void executePendingAction();
    void drawConfirmDialog();

    // ── Build / log ──────────────────────────────────────────────────────────
    std::vector<std::string> log_;
    bool showBuildLog_ = true;

    // ── Cursors ──────────────────────────────────────────────────────────────
    SDL_Cursor* cursorArrow_     = nullptr;
    SDL_Cursor* cursorCrosshair_ = nullptr;
    SDL_Cursor* cursorHand_      = nullptr;
    SDL_Cursor* cursorMove_      = nullptr;

    // ── File Editor ──────────────────────────────────────────────────────────
    struct OpenFile {
        std::string path;
        std::string content;
        bool        modified = false;
        // Undo / redo stacks
        std::vector<std::string> undoStack;
        std::vector<std::string> redoStack;
        std::string              lastSnapshot; // last committed state
    };
    std::vector<OpenFile> openFiles_;
    int                   activeFileTab_ = -1;
    std::string           fileBrowserRoot_;
    void openFileInEditor(const std::string& path);
    void saveOpenFile(int idx);
    void snapshotForUndo(OpenFile& f);
    void undoFile(OpenFile& f);
    void redoFile(OpenFile& f);

    // ── Layout ───────────────────────────────────────────────────────────────
    bool layoutInitialized_ = false;
    void buildDefaultLayout(ImGuiID dockspaceId);

    // ── UI Panels ────────────────────────────────────────────────────────────
    void drawMenuBar();
    void drawToolbar();
    void drawSceneHierarchy();
    void drawPropertiesPanel();
    void drawTilePalette();
    void drawViewport();
    void drawBuildLog();
    void drawFileBrowser();
    void drawFileEditor();
    void drawOpenDialog();
    void drawSaveDialog();

    // ── Actions ──────────────────────────────────────────────────────────────
    void newScene();
    void refreshSceneFiles();
    void saveScene(const std::string& path);
    void openScene(const std::string& path);
    void buildAndRun();
    void applySceneToWorld();

    // ── Viewport helpers ─────────────────────────────────────────────────────
    void renderWorldToTexture();
    bool viewportScreenToWorld(float vx, float vy, float& wx, float& wy);
    void handleToolClick(float wx, float wy);
    void paintTileAt(float wx, float wy);

    void addLog(const std::string& msg);
};
