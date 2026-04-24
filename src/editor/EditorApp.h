#pragma once
#include <SDL2/SDL.h>
#include "imgui.h"
#include "SceneData.h"
#include "World.h"
#include "CommandStack.h"
#include "AssetDatabase.h"
#include "ImportManager.h"
#include "FileWatcher.h"
#include "AssetBrowserPanel.h"
#include "AssetInspectorPanel.h"
#include "PlaySession.h"
#include "Game.h"
#include "Reflection.h"
#include "EntityRegistry.h"
#include "ContentValidator.h"
#include "ValidationPanel.h"
#include "SpriteEditorPanel.h"
#include "WelcomePanel.h"
#include "EntityViewportPanel.h"
#include "CliffBrushCommand.h"
#include "TexturePaintCommand.h"
#include "WaterLevelCommand.h"
#include "project/ProjectManager.h"
#include "EditorVkContext.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <unordered_map>
#include <filesystem>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// EditorApp – Unreal-style level editor for the Isometric RPG
// ─────────────────────────────────────────────────────────────────────────────
class EditorApp {
public:
    bool init(const std::string& projectPath = "");
    void run();
    ~EditorApp();

private:
    SDL_Window*      window_      = nullptr;
    EditorVkContext  vkCtx_;
    bool             running_     = false;

    // ── Editor mode ──────────────────────────────────────────────────────────
    enum class EditorMode { Edit, Play };
    EditorMode  editorMode_ = EditorMode::Edit;
    PlaySession playSession_;
    std::unique_ptr<Game> playGame_;     // live game instance during Play
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

    // ── Project ───────────────────────────────────────────────────────────────
    ProjectManager projectManager_;
    // Open a .dashproject file and refresh all dependent editor paths.
    bool openProject(const std::string& manifestPath);
    // Create a new project folder + .dashproject and open it.
    bool createProject(const std::string& dirPath, const std::string& name);
    // Re-sync assetsRoot_, libraryRoot_, scenesDir_ from active project (or AppPaths).
    void refreshProjectPaths();

    // ── Asset Database ────────────────────────────────────────────────────────
    AssetDatabase  assetDb_;
    std::string    assetDbPath_;
    ImportManager  importManager_;
    std::string    assetsRoot_;
    std::string    libraryRoot_;
    AssetBrowserPanel   assetBrowserPanel_;
    AssetInspectorPanel assetInspectorPanel_;
    // ── Hot-reload ──────────────────────────────────────────────────
    FileWatcher fileWatcher_;
    bool        autoReload_ = true;
    std::vector<FileWatcher::FileChange> deferredReloads_;

    // ── Content Validation ─────────────────────────────────────────
    ContentValidator             contentValidator_;
    ValidationPanel              validationPanel_;
    std::vector<ValidationIssue> validationIssues_;
    bool                         showValidationPanel_ = false;
    bool                         showAboutModal_      = false;
    bool                         showMigrationLogModal_ = false;
    bool                         migrationLastSuccess_ = false;
    std::string                  migrationSummaryText_;
    std::string                  migrationLogText_;

    // ── Sprite Editor ─────────────────────────────────────────────────────
    SpriteEditorPanel spriteEditor_;

    struct SpritePivotMeta {
        float pivotX = 0.5f;
        float pivotY = 1.0f;
        std::filesystem::file_time_type mtime{};
        bool hasMtime = false;
    };
    std::unordered_map<std::string, SpritePivotMeta> spritePivotCache_;
    // ── Camera ───────────────────────────────────────────────────────────────
    float camX_ = 12.f;
    float camY_ = 12.f;

    struct Viewport3DState {
        bool  useVulkan3D = true;
        bool  embeddedPreview = false;
        float isoYawDeg = 45.0f;
        float isoPitchDeg = 50.0f;
        float cameraDistance = 18.0f;
        float cameraHeight = 1.5f;
        float zoom = 1.0f;
        float heightScale = 32.0f;
        float gridOpacity = 0.22f;
        bool  fogEnabled = true;
        float fogStart = 60.0f;
        float fogEnd = 120.0f;
        // Directional light (daylight defaults)
        float lightDirX = 0.3f, lightDirY = 0.9f, lightDirZ = 0.2f;
        float lightColorR = 1.0f, lightColorG = 0.98f, lightColorB = 0.92f;
        float lightIntensity = 1.3f;
        float ambientStrength = 0.55f;
        float specularStrength = 0.15f;
        float specularShininess = 32.0f;
    } viewport3D_;

    // Displayed size of the viewport image (for mouse coordinate mapping)
    float vpDisplayW_ = 1.f;
    float vpDisplayH_ = 1.f;
    float vpScreenX_ = 0.f;
    float vpScreenY_ = 0.f;

    // ── Tools ────────────────────────────────────────────────────────────────
    enum class Tool { Select, PaintTile, FillTile, EyeDropper, HeightBrush, CliffBrush, TexturePaint, WaterTool, PlaceEnemy, Erase };
    Tool     currentTool_      = Tool::Select;
    TileType selectedTileType_ = TileType::Grass;
    int      brushSize_        = 1;

    // Height brush settings
    enum class HeightBrushMode { Raise, Lower, Smooth, Flatten };
    HeightBrushMode heightBrushMode_     = HeightBrushMode::Raise;
    float           heightBrushStrength_ = 0.05f;
    int             heightBrushRadius_   = 2;

    // Cliff brush settings (WC3-style)
    CliffBrushCommand::Mode cliffBrushMode_ = CliffBrushCommand::Mode::Raise;
    int cliffBrushRadius_ = 1;

    // Texture paint settings (WC3-style)
    TerrainTextureId selectedTexture_ = TerrainTextureId::Grass;
    float            texturePaintStrength_ = 0.8f;
    int              texturePaintRadius_ = 2;

    // Water tool settings (WC3-style)
    float waterLevel_ = 2.4f;
    int   selectedWaterBodyId_ = 1;

    // ── File dialogs ─────────────────────────────────────────────────────────
    std::string              scenesDir_;
    std::vector<std::string> sceneFiles_;
    std::string              selectedSceneFile_;
    bool                     showSceneSelector_ = true;
    bool  showOpenDialog_ = false;
    bool  showSaveDialog_ = false;
    bool  showCreateSceneDialog_ = false;
    char  saveFileName_[256] = "scene.json";
    char  createSceneFileName_[256] = "new_scene.json";

    // ── Unsaved-changes confirmation ─────────────────────────────────────────
    enum class PendingAction { None, NewScene, OpenScene, Exit };
    PendingAction pendingAction_     = PendingAction::None;
    bool          showConfirmDialog_ = false;
    void requestAction(PendingAction action);
    void executePendingAction();
    void drawConfirmDialog();

    // ── Build / log ──────────────────────────────────────────────────────────
    std::vector<std::string> log_;
    bool playAuditActive_ = false;
    std::string playAuditSessionStartedAt_;
    std::vector<std::string> playAuditCurrentSessionLogs_;
    bool showToolbar_ = true;
    bool showSceneHierarchy_ = true;
    bool showPropertiesPanel_ = true;
    bool showTilePalette_ = true;
    bool showViewport_ = true;
    bool showBuildLog_ = true;
    bool showPerformancePanel_ = true;
    bool showFileBrowser_ = true;
    bool showFileEditor_ = true;
    bool showAssetBrowser_ = true;
    bool showAssetInspector_ = true;
    bool showLightingPanel_ = true;
    bool showEntityViewport_ = false;
    WelcomePanel welcomePanel_;
    EntityViewportPanel entityViewportPanel_;

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
    char                  fileBrowserNavBuf_[512] = {};
    char                  fileBrowserFilter_[128] = {};
    void openFileInEditor(const std::string& path);
    void saveOpenFile(int idx);
    void snapshotForUndo(OpenFile& f);
    void undoFile(OpenFile& f);
    void redoFile(OpenFile& f);

    // ── Entity drag state (viewport drag-to-move) ────────────────────────────
    bool     draggingEntity_  = false;
    float    dragStartX_      = 0.f;   // world pos when drag began
    float    dragStartY_      = 0.f;

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
    void drawPerformancePanel();
    void drawFileBrowser();
    void drawFileEditor();
    void drawSceneSelector();
    void drawOpenDialog();
    void drawSaveDialog();
    void drawCreateSceneDialog();
    void drawMigrationLogModal();
    void drawLightingPanel();

    // ── Actions ──────────────────────────────────────────────────────────────
    void newScene();
    void refreshSceneFiles();
    void saveScene(const std::string& path);
    void openScene(const std::string& path);
    void focusCameraOnEntities();
    void loadInitialProjectScene();
    void buildAndRun();
    void exportGameBundle();
    void applySceneToWorld();

    // ── Viewport helpers ─────────────────────────────────────────────────────
    void renderWorldToTexture();
    void getSpritePivot(const std::string& spriteName, float& outPivotX, float& outPivotY);
    Vec2f worldToScreenIso3D(float wx, float wy, float wz) const;
    float entityWorldZ(uint64_t entityId) const;
    float tileHeight(TileType type) const;
    bool syncSceneRender3DSettingsFromUI();
    void syncUIRender3DSettingsFromScene();
    bool viewportScreenToWorld(float vx, float vy, float& wx, float& wy);
    void buildViewProjMatrix(float vpW, float vpH, float viewProj[16],
                             float* outEyeX = nullptr, float* outEyeY = nullptr,
                             float* outEyeZ = nullptr);
    void handleToolClick(float wx, float wy);
    void paintTileAt(float wx, float wy);
    void floodFillAt(float wx, float wy);
    void heightBrushAt(float wx, float wy);
    void cliffBrushAt(float wx, float wy);
    void texturePaintAt(float wx, float wy);
    void waterToolAt(float wx, float wy);

    void addLog(const std::string& msg);
    void beginPlayAuditSession();
    void flushPlayAuditSessionToFile(const std::string& reason);
    std::string playAuditFilePath() const;

    // Persist and restore asset DB, file watcher from current assetsRoot_/libraryRoot_.
    void reinitAssetPipeline();
};
