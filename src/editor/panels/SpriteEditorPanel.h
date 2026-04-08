#pragma once
#include <SDL2/SDL.h>
#include "imgui.h"
#include <string>
#include <vector>
#include <array>
#include <deque>

// ─────────────────────────────────────────────────────────────────────────────
// SpriteEditorPanel — pixel-art sprite editor integrated in the editor UI.
//
// D37: Canvas with STREAMING texture, zoom, and grid overlay.
// D38: Pencil, Eraser, Flood-fill, Eyedropper tools.
// D39: Line, Rect, Select + copy/paste.
// D40: FG/BG color, 32-color palette, color history.
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

class SpriteEditorPanel {
public:
    SpriteEditorPanel() = default;
    ~SpriteEditorPanel();

    void init(SDL_Renderer* r);
    void draw();

    // External access (set by EditorApp so Assign button works)
    uint64_t*   selectedEntityId = nullptr;   // pointer into EditorApp
    std::string currentPath;                  // full path to the open PNG

    bool isOpen = false;

private:
    // ── Renderer ─────────────────────────────────────────────────────────────
    SDL_Renderer* renderer_  = nullptr;
    SDL_Texture*  canvasTex_ = nullptr;

    // ── Canvas dimensions & zoom ─────────────────────────────────────────────
    int   canvasW_ = 16;
    int   canvasH_ = 16;
    float zoom_    = 16.f;   // screen pixels per canvas pixel

    // ── Pixel buffer (ABGR8888, SDL_PIXELFORMAT_ABGR8888) ────────────────────
    std::vector<uint32_t> pixels_;   // size = canvasW_ * canvasH_
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

    // ── Checkerboard (alpha background) ──────────────────────────────────────
    SDL_Texture* checkerTex_ = nullptr;

    // ── Private helpers ───────────────────────────────────────────────────────
    void newCanvas(int w, int h);
    void rebuildTexture();
    void uploadToGPU();
    void buildCheckerboard();

    // Drawing
    void drawToolbar();
    void drawColorSection();
    void drawCanvasArea();
    void drawGrid(ImDrawList* dl, ImVec2 origin, float cell, int cols, int rows) const;
    void drawNewModal();
    void drawSaveModal();

    // Tools
    void applyPencil(int x, int y);
    void applyEraser(int x, int y);
    void applyFill(int x, int y);
    void applyLine(int x0, int y0, int x1, int y1, uint32_t color);
    void applyRect(int x0, int y0, int x1, int y1, bool filled);
    void bakeGeometryDrag(int x1, int y1);

    // Pixel helpers
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
