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
#include "PlaybackController.h"
#include "Game.h"
#include "Reflection.h"
#include "EntityRegistry.h"
#include "ContentValidator.h"
#include "ValidationPanel.h"
#include "RuntimeInspectorPanel.h"
#include "BoneStructurePanel.h"
#include "SpriteEditorPanel.h"
#include "AudioPanel.h"
#include "WelcomePanel.h"
#include "EntityViewportPanel.h"
#include "FileEditorPanel.h"
#include "CliffBrushCommand.h"
#include "TexturePaintCommand.h"
#include "WaterLevelCommand.h"
#include "EntityHierarchy.h"
#include "gizmos/TransformGizmo.h"
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

    // ── Play-mode transport (pause / step / slow motion) ─────────────────────
    dash::playmode::PlaybackController playback_;
    void drawPlaybackControls();
    void handlePlaybackShortcut(SDL_Keycode key);
    // Path polled by the runtime (VulkanBootstrap --state).
    std::string playbackStatePath() const;
    void syncPlaybackStateFile(bool force = false);
    std::string playbackSpeedLabel() const;

    // ── Scene ────────────────────────────────────────────────────────────────
    SceneData    scene_;
    World        world_;
    uint64_t     selectedEntityId_ = 0;   // active entity (0 = no selection)
    std::vector<uint64_t> selection_;     // multi-selection; back() is active
    CommandStack commandStack_;

    EntityData* findEntityById(uint64_t id);
    void performUndo();
    void performRedo();

    // ── Selection ────────────────────────────────────────────────────────────
    bool isEntitySelected(uint64_t id) const;
    void setSelection(uint64_t id);            // replaces the whole selection
    void toggleSelection(uint64_t id);         // Ctrl/Cmd+click behaviour
    void setSelection(const std::vector<uint64_t>& ids);
    void clearSelection();
    void pruneSelection();                     // drop ids no longer in the scene

    // ── Hierarchy ────────────────────────────────────────────────────────────
    void drawHierarchyNode(uint64_t entityId, int depth);
    void reparentEntity(uint64_t childId, uint64_t newParentId);

    // ── Transform gizmos ─────────────────────────────────────────────────────
    struct GizmoDragEntry {
        uint64_t                  entityId = 0;
        dash::editor::Transform3D startLocal{};
        dash::editor::Transform3D startWorld{};
    };
    dash::gizmo::TransformGizmo gizmo_;
    std::vector<GizmoDragEntry> gizmoDrag_;
    dash::editor::Transform3D   gizmoDragPivot_{};
    float gizmoTranslateSnapTiles_ = 0.5f;
    float gizmoRotateSnapDeg_      = 15.0f;
    float gizmoScaleSnap_          = 0.1f;
    bool  gizmoAlwaysSnap_         = false;

    dash::gizmo::Vec3 entityGizmoPivot(uint64_t entityId);
    bool selectionGizmoPivot(dash::gizmo::Vec3& outPivot, dash::editor::Transform3D& outScenePivot);
    // Returns true when the gizmo owns the pointer this frame.
    bool updateViewportGizmo(const float viewProj[16], const dash::gizmo::ViewportRect& rect,
                             float mouseX, float mouseY, bool viewportHovered);
    void drawSelectionOverlays(ImDrawList* dl, const float viewProj[16],
                               const dash::gizmo::ViewportRect& rect);
    void handleGizmoShortcuts(bool viewportFocused);

    // ── Rectangle selection ──────────────────────────────────────────────────
    bool  rectSelecting_ = false;
    bool  rectSelectPending_ = false;
    float rectStartX_ = 0.f, rectStartY_ = 0.f;

    // Drives the marquee and draws it; call once per frame from drawViewport().
    void updateRectSelection(const float viewProj[16], const dash::gizmo::ViewportRect& rect,
                             float mouseX, float mouseY, bool viewportHovered,
                             bool gizmoOwnsPointer);
    std::vector<uint64_t> entitiesInScreenRect(const float viewProj[16],
                                               const dash::gizmo::ViewportRect& rect,
                                               float x0, float y0, float x1, float y1);

    // ── Inspector edits ──────────────────────────────────────────────────────
    // Spreads one field edit over every selected entity that owns the component.
    void applyComponentFieldEdit(uint64_t entityId, ComponentType compType,
                                 const PropertyInfo& prop,
                                 const PropertyValue& oldVal, const PropertyValue& newVal);

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
    ValidationPanel              validationPanel_;    std::vector<ValidationIssue> validationIssues_;
    bool                         showValidationPanel_ = false;
    // ── Runtime inspection ────────────────────────────
    RuntimeInspectorPanel        runtimeInspectorPanel_;
    bool                         showRuntimeInspector_ = false;    bool                         showAboutModal_      = false;
    // ── Skeleton authoring ────────────────────────────
    BoneStructurePanel           boneStructurePanel_;
    bool                         showBoneStructurePanel_ = false;
    bool                         showMigrationLogModal_ = false;
    bool                         migrationLastSuccess_ = false;
    std::string                  migrationSummaryText_;
    std::string                  migrationLogText_;

    // ── Sprite Editor ─────────────────────────────────────────────────────
    SpriteEditorPanel spriteEditor_;

    // ── Audio ─────────────────────────────────────────────────────────────
    AudioPanel audioPanel_;
    bool       showAudioPanel_ = false;

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
    bool showLightsPanel_ = true;
    bool showEntityViewport_ = false;
    WelcomePanel welcomePanel_;
    EntityViewportPanel entityViewportPanel_;

    // ── Cursors ──────────────────────────────────────────────────────────────
    SDL_Cursor* cursorArrow_     = nullptr;
    SDL_Cursor* cursorCrosshair_ = nullptr;
    SDL_Cursor* cursorHand_      = nullptr;
    SDL_Cursor* cursorMove_      = nullptr;

    // ── File Editor ──────────────────────────────────────────────────────────
    FileEditorPanel fileEditorPanel_;

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
    void drawSceneSelector();
    void drawOpenDialog();
    void drawSaveDialog();
    void drawCreateSceneDialog();
    void drawMigrationLogModal();
    void drawLightingPanel();
    void drawLightsPanel();

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
    void createLightEntity(LightType type);
    void commitFieldEdit(uint64_t primaryId, ComponentType compType,
                         const PropertyInfo& prop,
                         const PropertyValue& oldVal, const PropertyValue& newVal);
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
