#pragma once
#include <SDL2/SDL.h>
#include "imgui.h"
#include <string>
#include <vector>
#include <array>
#include <deque>

// Forward declarations for D42 integration
struct SceneData;
class CommandStack;
class ImportManager;
class World;

// ─────────────────────────────────────────────────────────────────────────────
// SpriteEditorPanel — pixel-art sprite editor integrated in the editor UI.
//
// D37: Canvas with STREAMING texture, zoom, and grid overlay.
// D38: Pencil, Eraser, Flood-fill, Eyedropper tools.
// D39: Line, Rect, Select + copy/paste.
// D40: FG/BG color, 32-color palette, color history.
// D41: Layers with alpha-over composition.
// ─────────────────────────────────────────────────────────────────────────────

enum class SpriteTool {
    Pencil,
    Eraser,
    Fill,
    Eyedropper,
    Line,
    Rect,
    Select,
};

enum class SpriteAnchor {
    TopLeft, TopCenter, TopRight,
    MiddleLeft, Center, MiddleRight,
    BottomLeft, BottomCenter, BottomRight,
    Custom,
};

// ── Layer ─────────────────────────────────────────────────────────────────────
struct SpriteLayer {
    std::string           name    = "Layer";
    std::vector<uint32_t> pixels;   // canvasW * canvasH, ABGR8888
    float                 opacity = 1.f;
    bool                  visible = true;
};

class SpriteEditorPanel {
public:
    SpriteEditorPanel() = default;
    ~SpriteEditorPanel();

    void init(SDL_Renderer* r);
    void draw();

    // External access (set by EditorApp so Assign button works)
    uint64_t*     selectedEntityId = nullptr;   // pointer into EditorApp
    SceneData*    scene            = nullptr;   // for Assign panel
    CommandStack* commandStack     = nullptr;   // for undoable Assign
    World*        world            = nullptr;   // for command execution
    ImportManager* importManager   = nullptr;   // for post-save reimport
    std::string*  assetsRoot       = nullptr;   // e.g. "/path/to/assets"
    std::string*  libraryRoot      = nullptr;   // for reimport

    std::string currentPath;                   // full path to the open PNG

    bool isOpen = false;

private:
    // ── Renderer ─────────────────────────────────────────────────────────────
    SDL_Renderer* renderer_  = nullptr;
    SDL_Texture*  canvasTex_ = nullptr;

    // ── Canvas dimensions & zoom ─────────────────────────────────────────────
    int   canvasW_ = 16;
    int   canvasH_ = 16;
    float zoom_    = 16.f;   // screen pixels per canvas pixel

    // ── Layers (D41) ──────────────────────────────────────────────────────────
    std::vector<SpriteLayer> layers_;
    int activeLayer_ = 0;

    // ── Composite buffer — result of all visible layers blended (sent to GPU) ─
    std::vector<uint32_t> composite_;   // size = canvasW_ * canvasH_
    bool dirty_ = false;

    // ── Tools ─────────────────────────────────────────────────────────────────
    SpriteTool  currentTool_   = SpriteTool::Pencil;
    SpriteTool  prevTool_      = SpriteTool::Pencil;   // restored after Eyedropper
    int         brushSize_     = 1;   // pixels in each direction from centre

    // ── Colors ────────────────────────────────────────────────────────────────
    uint32_t fgColor_ = 0xFF000000u;   // solid black (ABGR: A=FF, B=0, G=0, R=0)
    uint32_t bgColor_ = 0x00000000u;   // transparent
    std::array<uint32_t, 32> palette_{};
    std::deque<uint32_t> colorHistory_;  // newest at front, max 16
    int editingPaletteSlot_ = -1;

    // ── Geometry tool drag state ──────────────────────────────────────────────
    bool  dragging_  = false;
    int   dragX0_    = 0, dragY0_ = 0;   // canvas coords of drag start
    int   prevPixX_  = -1, prevPixY_ = -1;   // for Bresenham continuity

    // ── Selection ─────────────────────────────────────────────────────────────
    int selX_ = -1, selY_ = -1, selW_ = 0, selH_ = 0;   // -1 = no selection
    std::vector<uint32_t> clipboard_;
    int clipW_ = 0, clipH_ = 0;

    // ── New-canvas modal ──────────────────────────────────────────────────────
    bool showNewModal_   = false;
    int  newW_           = 16;
    int  newH_           = 16;
    char saveNameBuf_[128] = "sprite";
    bool showSaveModal_  = false;

    // ── Open-sprite modal (D42) ───────────────────────────────────────────────
    bool showOpenModal_  = false;
    std::vector<std::string> spriteFiles_;   // populated when modal opens
    int  selectedSpriteIdx_ = -1;

    // ── Assign-to-entity panel (D42) ─────────────────────────────────────────
    bool showAssignPanel_ = false;
    bool showCloseConfirm_ = false;
    bool pendingCloseAfterSave_ = false;

    // ── D44: Iso preview + anchor metadata ──────────────────────────────────
    SpriteAnchor anchor_ = SpriteAnchor::BottomCenter;
    float pivotX_ = 0.5f;   // normalized [0..1] inside sprite width
    float pivotY_ = 1.0f;   // normalized [0..1] inside sprite height
    enum class PreviewBg { Checker, Black, White };
    PreviewBg previewBg_ = PreviewBg::Checker;

    // ── Layer inline-edit state ───────────────────────────────────────────────
    int  renamingLayer_  = -1;
    char renameLayerBuf_[64] = {};

    // ── Checkerboard (alpha background) ──────────────────────────────────────
    SDL_Texture* checkerTex_ = nullptr;

    // ── Private helpers ───────────────────────────────────────────────────────
    void newCanvas(int w, int h);
    void rebuildTexture();
    void uploadToGPU();
    void buildCheckerboard();

    // Composition (D41)
    void compositeLayers();
    static uint32_t alphaOver(uint32_t dst, uint32_t src, float opacity);

    // Layer management (D41)
    void addLayer();
    void deleteActiveLayer();
    void moveLayerUp();
    void moveLayerDown();
    void mergeLayerDown();
    void flattenAll();

    // Drawing
    void drawToolbar();
    void drawColorSection();
    void drawCanvasArea();
    void drawLayersPanel();
    void drawGrid(ImDrawList* dl, ImVec2 origin, float cell, int cols, int rows) const;
    void drawNewModal();
    void drawSaveModal();
    void drawOpenModal();       // D42
    void drawAssignPanel();     // D42
    void drawCloseConfirmModal(); // D45
    void drawIsoPreviewPanel(); // D44

    // Save / Load helpers (D42)
    bool saveAsPNG(const std::string& path);
    bool loadFromPNG(const std::string& path);
    bool saveSpriteMeta(const std::string& pngPath) const;
    void loadSpriteMeta(const std::string& pngPath);
    static const char* anchorToString(SpriteAnchor a);
    static SpriteAnchor anchorFromString(const std::string& s);
    void setPivotFromAnchor(SpriteAnchor a);

    // Tools
    void applyPencil(int x, int y);
    void applyEraser(int x, int y);
    void applyFill(int x, int y);
    void applyLine(int x0, int y0, int x1, int y1, uint32_t color);
    void applyRect(int x0, int y0, int x1, int y1, bool filled);
    void bakeGeometryDrag(int x1, int y1);

    // Pixel helpers — operate on the ACTIVE LAYER
    void setPixel(int x, int y, uint32_t color);
    uint32_t getPixel(int x, int y) const;
    bool inBounds(int x, int y) const { return x >= 0 && x < canvasW_ && y >= 0 && y < canvasH_; }

    // Color conversion helpers (ABGR8888 ↔ ImVec4)
    static ImVec4     abgrToImVec4(uint32_t c);
    static uint32_t   imVec4ToAbgr(ImVec4 c);

    // D40 palette + history
    void initDefaultPalette();
    void loadPaletteFromDisk();
    void savePaletteToDisk() const;
    void pushColorHistory(uint32_t color);
};
