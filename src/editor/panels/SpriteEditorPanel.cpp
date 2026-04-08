#include "SpriteEditorPanel.h"
#include "IconsFontAwesome6.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "AppPaths.h"
#include <imgui.h>
#include <SDL2/SDL.h>
#include <cstring>
#include <queue>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ─── Color helpers ────────────────────────────────────────────────────────────
// SDL_PIXELFORMAT_ABGR8888 memory layout on little-endian:
//   bits 31-24 = A, bits 23-16 = B, bits 15-8 = G, bits 7-0 = R
// So the uint32 value stores: 0xAABBGGRR

ImVec4 SpriteEditorPanel::abgrToImVec4(uint32_t c)
{
    float r = ((c >>  0) & 0xFF) / 255.f;
    float g = ((c >>  8) & 0xFF) / 255.f;
    float b = ((c >> 16) & 0xFF) / 255.f;
    float a = ((c >> 24) & 0xFF) / 255.f;
    return { r, g, b, a };
}

uint32_t SpriteEditorPanel::imVec4ToAbgr(ImVec4 c)
{
    auto R = static_cast<uint32_t>(c.x * 255.f + 0.5f);
    auto G = static_cast<uint32_t>(c.y * 255.f + 0.5f);
    auto B = static_cast<uint32_t>(c.z * 255.f + 0.5f);
    auto A = static_cast<uint32_t>(c.w * 255.f + 0.5f);
    return (A << 24) | (B << 16) | (G << 8) | R;
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────
SpriteEditorPanel::~SpriteEditorPanel()
{
    if (canvasTex_)  SDL_DestroyTexture(canvasTex_);
    if (checkerTex_) SDL_DestroyTexture(checkerTex_);
}

void SpriteEditorPanel::init(SDL_Renderer* r)
{
    renderer_ = r;
    initDefaultPalette();
    loadPaletteFromDisk();
    newCanvas(16, 16);
    buildCheckerboard();
}

// ─── Palette + history (D40) ────────────────────────────────────────────────
void SpriteEditorPanel::initDefaultPalette()
{
    // PICO-8-inspired 32 color palette in RGBA (0xRRGGBBAA)
    static const uint32_t kDefaultRGBA[32] = {
        0x000000FFu, 0x1D2B53FFu, 0x7E2553FFu, 0x008751FFu,
        0xAB5236FFu, 0x5F574FFFu, 0xC2C3C7FFu, 0xFFF1E8FFu,
        0xFF004DFFu, 0xFFA300FFu, 0xFFEC27FFu, 0x00E436FFu,
        0x29ADFFFFu, 0x83769CFFu, 0xFF77A8FFu, 0xFFCCAAFFu,
        0x291814FFu, 0x111D35FFu, 0x422136FFu, 0x125359FFu,
        0x742F29FFu, 0x49333BFFu, 0xA28879FFu, 0xF3EF7DFFu,
        0xBE1250FFu, 0xFF6C24FFu, 0xA8E72EFFu, 0x00B543FFu,
        0x065AB5FFu, 0x754665FFu, 0xFF6E59FFu, 0xFF9D81FFu
    };

    auto rgbaToAbgr = [](uint32_t rgba) {
        uint32_t r = (rgba >> 24) & 0xFFu;
        uint32_t g = (rgba >> 16) & 0xFFu;
        uint32_t b = (rgba >> 8)  & 0xFFu;
        uint32_t a = (rgba >> 0)  & 0xFFu;
        return (a << 24) | (b << 16) | (g << 8) | r;
    };

    for (size_t i = 0; i < palette_.size(); ++i) {
        palette_[i] = rgbaToAbgr(kDefaultRGBA[i]);
    }
}

void SpriteEditorPanel::loadPaletteFromDisk()
{
    const fs::path path = fs::path(AppPaths::getResourcesDir()) / "assets" / "sprites" / "default_palette.json";
    std::ifstream in(path);
    if (!in) return;

    try {
        json j;
        in >> j;
        if (!j.is_object() || !j.contains("colors") || !j["colors"].is_array()) return;

        auto parseRgba = [](const json& value, uint32_t fallbackRgba) {
            if (value.is_number_unsigned()) {
                return static_cast<uint32_t>(value.get<uint64_t>() & 0xFFFFFFFFu);
            }
            if (value.is_number_integer()) {
                return static_cast<uint32_t>(value.get<int64_t>() & 0xFFFFFFFFu);
            }
            if (value.is_string()) {
                std::string s = value.get<std::string>();
                if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
                std::stringstream ss;
                ss << std::hex << s;
                uint32_t parsed = 0;
                if (ss >> parsed) return parsed;
            }
            return fallbackRgba;
        };

        for (size_t i = 0; i < palette_.size() && i < j["colors"].size(); ++i) {
            uint32_t fallbackAbgr = palette_[i];
            uint32_t fallbackRgba = ((fallbackAbgr & 0x000000FFu) << 24)
                                  | ((fallbackAbgr & 0x0000FF00u) << 8)
                                  | ((fallbackAbgr & 0x00FF0000u) >> 8)
                                  | ((fallbackAbgr & 0xFF000000u) >> 24);
            uint32_t rgba = parseRgba(j["colors"][i], fallbackRgba);
            uint32_t r = (rgba >> 24) & 0xFFu;
            uint32_t g = (rgba >> 16) & 0xFFu;
            uint32_t b = (rgba >> 8)  & 0xFFu;
            uint32_t a = (rgba >> 0)  & 0xFFu;
            palette_[i] = (a << 24) | (b << 16) | (g << 8) | r;
        }
    } catch (...) {
        // Ignore invalid palette files and keep defaults.
    }
}

void SpriteEditorPanel::savePaletteToDisk() const
{
    const fs::path path = fs::path(AppPaths::getResourcesDir()) / "assets" / "sprites" / "default_palette.json";
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    json j;
    j["colors"] = json::array();
    for (uint32_t abgr : palette_) {
        uint32_t rgba = ((abgr & 0x000000FFu) << 24)
                      | ((abgr & 0x0000FF00u) << 8)
                      | ((abgr & 0x00FF0000u) >> 8)
                      | ((abgr & 0xFF000000u) >> 24);

        std::ostringstream ss;
        ss << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << rgba;
        j["colors"].push_back(ss.str());
    }

    std::ofstream out(path);
    if (out) out << j.dump(2);
}

void SpriteEditorPanel::pushColorHistory(uint32_t color)
{
    auto it = std::find(colorHistory_.begin(), colorHistory_.end(), color);
    if (it != colorHistory_.end()) {
        colorHistory_.erase(it);
    }
    colorHistory_.push_front(color);
    while (colorHistory_.size() > 16) colorHistory_.pop_back();
}

// ─── Canvas management ────────────────────────────────────────────────────────
void SpriteEditorPanel::newCanvas(int w, int h)
{
    canvasW_ = w;
    canvasH_ = h;
    pixels_.assign(static_cast<size_t>(w * h), 0u);  // fully transparent
    selX_ = selY_ = -1; selW_ = selH_ = 0;
    prevPixX_ = prevPixY_ = -1;
    dragging_ = false;
    currentPath.clear();
    rebuildTexture();
}

void SpriteEditorPanel::rebuildTexture()
{
    if (canvasTex_) { SDL_DestroyTexture(canvasTex_); canvasTex_ = nullptr; }
    canvasTex_ = SDL_CreateTexture(renderer_,
        SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STREAMING,
        canvasW_, canvasH_);
    if (canvasTex_) {
        SDL_SetTextureBlendMode(canvasTex_, SDL_BLENDMODE_BLEND);
        uploadToGPU();
    }
}

void SpriteEditorPanel::uploadToGPU()
{
    if (!canvasTex_) return;
    void* ptr; int pitch;
    if (SDL_LockTexture(canvasTex_, nullptr, &ptr, &pitch) == 0) {
        // pitch is in bytes; canvasW_ * 4 bytes per row
        for (int y = 0; y < canvasH_; ++y) {
            auto* dst = reinterpret_cast<uint32_t*>(
                static_cast<uint8_t*>(ptr) + y * pitch);
            const auto* src = pixels_.data() + y * canvasW_;
            std::memcpy(dst, src, static_cast<size_t>(canvasW_) * 4);
        }
        SDL_UnlockTexture(canvasTex_);
    }
    dirty_ = false;
}

void SpriteEditorPanel::buildCheckerboard()
{
    if (checkerTex_) { SDL_DestroyTexture(checkerTex_); checkerTex_ = nullptr; }
    constexpr int SZ = 8;
    checkerTex_ = SDL_CreateTexture(renderer_,
        SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STATIC,
        SZ * 2, SZ * 2);
    if (!checkerTex_) return;
    SDL_SetTextureBlendMode(checkerTex_, SDL_BLENDMODE_NONE);

    uint32_t light = 0xFFCCCCCCu;   // light grey (ABGR)
    uint32_t dark  = 0xFF999999u;   // dark grey
    uint32_t pixels[SZ * 2 * SZ * 2];
    for (int y = 0; y < SZ * 2; ++y)
        for (int x = 0; x < SZ * 2; ++x)
            pixels[y * SZ * 2 + x] = ((x / SZ + y / SZ) % 2 == 0) ? light : dark;
    SDL_UpdateTexture(checkerTex_, nullptr, pixels, SZ * 2 * 4);
}

// ─── Pixel primitives ─────────────────────────────────────────────────────────
void SpriteEditorPanel::setPixel(int x, int y, uint32_t color)
{
    if (!inBounds(x, y)) return;
    pixels_[static_cast<size_t>(y * canvasW_ + x)] = color;
    dirty_ = true;
}

uint32_t SpriteEditorPanel::getPixel(int x, int y) const
{
    if (!inBounds(x, y)) return 0u;
    return pixels_[static_cast<size_t>(y * canvasW_ + x)];
}

// ─── Tool implementations (pure pixel logic) ─────────────────────────────────
void SpriteEditorPanel::applyPencil(int x, int y)
{
    for (int dy = 0; dy < brushSize_; ++dy)
        for (int dx = 0; dx < brushSize_; ++dx)
            setPixel(x + dx, y + dy, fgColor_);
}

void SpriteEditorPanel::applyEraser(int x, int y)
{
    for (int dy = 0; dy < brushSize_; ++dy)
        for (int dx = 0; dx < brushSize_; ++dx)
            setPixel(x + dx, y + dy, 0u);
}

void SpriteEditorPanel::applyFill(int x, int y)
{
    if (!inBounds(x, y)) return;
    uint32_t target = getPixel(x, y);
    if (target == fgColor_) return;

    std::queue<std::pair<int,int>> q;
    q.push({x, y});
    while (!q.empty()) {
        auto [cx, cy] = q.front(); q.pop();
        if (!inBounds(cx, cy) || getPixel(cx, cy) != target) continue;
        setPixel(cx, cy, fgColor_);
        q.push({cx+1, cy}); q.push({cx-1, cy});
        q.push({cx, cy+1}); q.push({cx, cy-1});
    }
}

void SpriteEditorPanel::applyLine(int x0, int y0, int x1, int y1, uint32_t color)
{
    // Bresenham
    int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    while (true) {
        for (int dy2 = 0; dy2 < brushSize_; ++dy2)
            for (int dx2 = 0; dx2 < brushSize_; ++dx2)
                setPixel(x0 + dx2, y0 + dy2, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void SpriteEditorPanel::applyRect(int x0, int y0, int x1, int y1, bool filled)
{
    int minX = std::min(x0, x1), maxX = std::max(x0, x1);
    int minY = std::min(y0, y1), maxY = std::max(y0, y1);
    if (filled) {
        for (int y = minY; y <= maxY; ++y)
            for (int x = minX; x <= maxX; ++x)
                setPixel(x, y, fgColor_);
    } else {
        for (int x = minX; x <= maxX; ++x) { setPixel(x, minY, fgColor_); setPixel(x, maxY, fgColor_); }
        for (int y = minY; y <= maxY; ++y) { setPixel(minX, y, fgColor_); setPixel(maxX, y, fgColor_); }
    }
}

void SpriteEditorPanel::bakeGeometryDrag(int x1, int y1)
{
    if (currentTool_ == SpriteTool::Line) {
        pushColorHistory(fgColor_);
        applyLine(dragX0_, dragY0_, x1, y1, fgColor_);
    } else if (currentTool_ == SpriteTool::Rect) {
        pushColorHistory(fgColor_);
        bool filled = ImGui::GetIO().KeyShift;
        applyRect(dragX0_, dragY0_, x1, y1, filled);
    } else if (currentTool_ == SpriteTool::Select) {
        selX_ = std::min(dragX0_, x1); selW_ = std::abs(x1 - dragX0_) + 1;
        selY_ = std::min(dragY0_, y1); selH_ = std::abs(y1 - dragY0_) + 1;
    }
}

// ─── Grid ─────────────────────────────────────────────────────────────────────
void SpriteEditorPanel::drawGrid(ImDrawList* dl, ImVec2 orig, float cell, int cols, int rows) const
{
    ImU32 col = IM_COL32(80, 80, 80, 120);
    for (int x = 0; x <= cols; ++x)
        dl->AddLine({orig.x + x * cell, orig.y},
                    {orig.x + x * cell, orig.y + rows * cell}, col);
    for (int y = 0; y <= rows; ++y)
        dl->AddLine({orig.x,            orig.y + y * cell},
                    {orig.x + cols * cell, orig.y + y * cell}, col);
}

// ─── Toolbar (tool buttons + brush size) ──────────────────────────────────────
void SpriteEditorPanel::drawToolbar()
{
    auto toolBtn = [&](const char* label, SpriteTool t) {
        bool active = (currentTool_ == t);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label, {36, 32})) {
            prevTool_    = currentTool_;
            currentTool_ = t;
        }
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", label);
        ImGui::SameLine();
    };

    toolBtn(ICON_FA_PENCIL,       SpriteTool::Pencil);
    toolBtn(ICON_FA_ERASER,       SpriteTool::Eraser);
    toolBtn(ICON_FA_FILL_DRIP,    SpriteTool::Fill);
    toolBtn(ICON_FA_EYE_DROPPER,  SpriteTool::Eyedropper);
    toolBtn(ICON_FA_MINUS,        SpriteTool::Line);
    toolBtn(ICON_FA_SQUARE,       SpriteTool::Rect);
    toolBtn(ICON_FA_OBJECT_GROUP, SpriteTool::Select);

    ImGui::SameLine(0, 12);
    ImGui::SetNextItemWidth(80);
    ImGui::SliderInt("Brush##sz", &brushSize_, 1, 5);
}

// ─── Color section ───────────────────────────────────────────────────────────
void SpriteEditorPanel::drawColorSection()
{
    // FG color swatch → open picker popup
    ImVec4 fg4 = abgrToImVec4(fgColor_);
    if (ImGui::ColorButton("##fg", fg4, ImGuiColorEditFlags_AlphaPreview, {24, 24}))
        ImGui::OpenPopup("##fgpick");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Foreground");
    if (ImGui::BeginPopup("##fgpick")) {
        ImGui::Text("Foreground Color");
        if (ImGui::ColorPicker4("##fgpicker", &fg4.x,
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview))
            fgColor_ = imVec4ToAbgr(fg4);
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    // BG color swatch
    ImVec4 bg4 = abgrToImVec4(bgColor_);
    if (ImGui::ColorButton("##bg", bg4, ImGuiColorEditFlags_AlphaPreview, {24, 24}))
        ImGui::OpenPopup("##bgpick");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Background");
    if (ImGui::BeginPopup("##bgpick")) {
        ImGui::Text("Background Color");
        if (ImGui::ColorPicker4("##bgpicker", &bg4.x,
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview))
            bgColor_ = imVec4ToAbgr(bg4);
        ImGui::EndPopup();
    }

    ImGui::SameLine(0, 8);
    ImGui::TextUnformatted("FG / BG  (X swap, D reset)");

    // Keyboard shortcuts for colors
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        if (!ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyDown(ImGuiKey_X)) std::swap(fgColor_, bgColor_);
            if (ImGui::IsKeyDown(ImGuiKey_D)) {
                fgColor_ = 0xFF000000u;
                bgColor_ = 0x00000000u;
            }
        }
    }

    if (ImGui::CollapsingHeader("Color Picker", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImVec4 picker = abgrToImVec4(fgColor_);
        if (ImGui::ColorPicker4("##inlinePicker", &picker.x,
            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview)) {
            fgColor_ = imVec4ToAbgr(picker);
        }

        float alpha = picker.w;
        if (ImGui::SliderFloat("Alpha##fg", &alpha, 0.f, 1.f, "%.2f")) {
            picker.w = alpha;
            fgColor_ = imVec4ToAbgr(picker);
        }
    }

    ImGui::TextUnformatted("Palette (LMB=FG, RMB=BG, double click=edit)");
    const float swatch = 14.f;
    for (int i = 0; i < 32; ++i) {
        ImGui::PushID(i);
        ImVec4 c = abgrToImVec4(palette_[static_cast<size_t>(i)]);
        ImGui::ColorButton("##pal", c, ImGuiColorEditFlags_AlphaPreview, {swatch, swatch});
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            fgColor_ = palette_[static_cast<size_t>(i)];
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            bgColor_ = palette_[static_cast<size_t>(i)];
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            editingPaletteSlot_ = i;
            ImGui::OpenPopup("Palette Slot Edit##popup");
        }
        ImGui::PopID();

        if ((i % 16) != 15) ImGui::SameLine();
    }

    if (ImGui::BeginPopupModal("Palette Slot Edit##popup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (editingPaletteSlot_ >= 0 && editingPaletteSlot_ < 32) {
            ImVec4 slot = abgrToImVec4(palette_[static_cast<size_t>(editingPaletteSlot_)]);
            if (ImGui::ColorPicker4("##slotpicker", &slot.x,
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview)) {
                palette_[static_cast<size_t>(editingPaletteSlot_)] = imVec4ToAbgr(slot);
            }
        }
        if (ImGui::Button("Close", {80, 0})) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::Button("Import Palette")) loadPaletteFromDisk();
    ImGui::SameLine();
    if (ImGui::Button("Export Palette")) savePaletteToDisk();

    ImGui::TextUnformatted("Recent Colors");
    for (size_t i = 0; i < colorHistory_.size(); ++i) {
        ImGui::PushID(static_cast<int>(1000 + i));
        ImGui::ColorButton("##hist", abgrToImVec4(colorHistory_[i]),
            ImGuiColorEditFlags_AlphaPreview, {swatch, swatch});
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) fgColor_ = colorHistory_[i];
        ImGui::PopID();
        if (i + 1 < colorHistory_.size()) ImGui::SameLine();
    }
}

// ─── Canvas area (image + grid + interaction) ─────────────────────────────────
void SpriteEditorPanel::drawCanvasArea()
{
    if (!canvasTex_) return;

    float cw = canvasW_ * zoom_;
    float ch = canvasH_ * zoom_;

    // Checkerboard background via SDL_RenderCopy handled by ImGui child scroll region
    ImGui::BeginChild("##canvas_scroll", {0, 0}, false,
        ImGuiWindowFlags_HorizontalScrollbar);

    ImVec2 origin = ImGui::GetCursorScreenPos();

    // Draw checkerboard under the sprite
    if (checkerTex_) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddImage(reinterpret_cast<ImTextureID>(checkerTex_),
            origin, {origin.x + cw, origin.y + ch},
            {0, 0}, {cw / 16.f, ch / 16.f});   // tile the 16×16 checker
    }

    // Sprite image
    ImGui::Image(reinterpret_cast<ImTextureID>(canvasTex_), {cw, ch});
    bool hovered = ImGui::IsItemHovered();

    ImVec2 mpos = ImGui::GetMousePos();
    int pixX = static_cast<int>((mpos.x - origin.x) / zoom_);
    int pixY = static_cast<int>((mpos.y - origin.y) / zoom_);
    pixX = std::clamp(pixX, 0, canvasW_ - 1);
    pixY = std::clamp(pixY, 0, canvasH_ - 1);

    // Grid overlay
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (zoom_ >= 4.f)
        drawGrid(dl, origin, zoom_, canvasW_, canvasH_);

    // Selection overlay (marching ants approximation via dashed rect)
    if (selX_ >= 0) {
        ImVec2 s0 = {origin.x + selX_ * zoom_,          origin.y + selY_ * zoom_};
        ImVec2 s1 = {origin.x + (selX_ + selW_) * zoom_, origin.y + (selY_ + selH_) * zoom_};
        float t = static_cast<float>(SDL_GetTicks()) / 300.f;
        dl->AddRect(s0, s1, IM_COL32(255,255,255,220), 0.f, 0, 1.5f);
        dl->AddRect(s0, s1, IM_COL32(0,0,0,200), 0.f, 0, 1.5f);
        (void)t; // animate later via dash offset if needed
    }

    // Geometry preview during drag
    if (dragging_ && (currentTool_ == SpriteTool::Line || currentTool_ == SpriteTool::Rect)) {
        ImVec2 a = {origin.x + dragX0_ * zoom_ + zoom_ * 0.5f,
                    origin.y + dragY0_ * zoom_ + zoom_ * 0.5f};
        ImVec2 b = {origin.x + pixX  * zoom_ + zoom_ * 0.5f,
                    origin.y + pixY  * zoom_ + zoom_ * 0.5f};
        if (currentTool_ == SpriteTool::Line)
            dl->AddLine(a, b, IM_COL32(255, 255, 0, 200), 1.5f);
        else
            dl->AddRect({origin.x + std::min(dragX0_, pixX) * zoom_,
                         origin.y + std::min(dragY0_, pixY) * zoom_},
                        {origin.x + (std::max(dragX0_, pixX) + 1) * zoom_,
                         origin.y + (std::max(dragY0_, pixY) + 1) * zoom_},
                        IM_COL32(255, 255, 0, 200), 0.f, 0, 1.5f);
    }

    // Status bar: pixel coordinates
    ImGui::TextDisabled("(%d, %d)  zoom: %.0fx", pixX, pixY, zoom_);

    // ── Mouse interaction ──────────────────────────────────────────────────
    if (hovered) {
        // Keyboard shortcuts for tools (only when canvas is focused)
        if (!ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyDown(ImGuiKey_P)) currentTool_ = SpriteTool::Pencil;
            if (ImGui::IsKeyDown(ImGuiKey_E)) currentTool_ = SpriteTool::Eraser;
            if (ImGui::IsKeyDown(ImGuiKey_G)) currentTool_ = SpriteTool::Fill;
            if (ImGui::IsKeyDown(ImGuiKey_I)) currentTool_ = SpriteTool::Eyedropper;
            if (ImGui::IsKeyDown(ImGuiKey_L)) currentTool_ = SpriteTool::Line;
            if (ImGui::IsKeyDown(ImGuiKey_R)) currentTool_ = SpriteTool::Rect;
            if (ImGui::IsKeyDown(ImGuiKey_S) && !ImGui::GetIO().KeyCtrl) currentTool_ = SpriteTool::Select;

            // Ctrl+C / Ctrl+V on selection
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyDown(ImGuiKey_C) && selX_ >= 0) {
                clipW_ = selW_; clipH_ = selH_;
                clipboard_.resize(static_cast<size_t>(clipW_ * clipH_));
                for (int y = 0; y < clipH_; ++y)
                    for (int x = 0; x < clipW_; ++x)
                        clipboard_[static_cast<size_t>(y * clipW_ + x)] = getPixel(selX_ + x, selY_ + y);
            }
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyDown(ImGuiKey_V) && !clipboard_.empty()) {
                for (int y = 0; y < clipH_; ++y)
                    for (int x = 0; x < clipW_; ++x)
                        setPixel(pixX + x, pixY + y,
                            clipboard_[static_cast<size_t>(y * clipW_ + x)]);
                dirty_ = true;
            }
            // Delete selection
            if ((ImGui::IsKeyDown(ImGuiKey_Delete) || ImGui::IsKeyDown(ImGuiKey_Backspace)) && selX_ >= 0) {
                for (int y = selY_; y < selY_ + selH_; ++y)
                    for (int x = selX_; x < selX_ + selW_; ++x)
                        setPixel(x, y, 0u);
                dirty_ = true;
            }
            // Escape = deselect
            if (ImGui::IsKeyDown(ImGuiKey_Escape)) { selX_ = selY_ = -1; selW_ = selH_ = 0; }
        }

        // Zoom with scroll
        if (std::abs(ImGui::GetIO().MouseWheel) > 0.01f) {
            zoom_ = std::clamp(zoom_ + ImGui::GetIO().MouseWheel * 2.f, 2.f, 32.f);
        }

        // Mouse down → start tool
        if (ImGui::IsMouseClicked(0)) {
            switch (currentTool_) {
            case SpriteTool::Pencil:
                pushColorHistory(fgColor_);
                applyPencil(pixX, pixY);
                prevPixX_ = pixX; prevPixY_ = pixY;
                break;
            case SpriteTool::Eraser:
                pushColorHistory(0u);
                applyEraser(pixX, pixY);
                prevPixX_ = pixX; prevPixY_ = pixY;
                break;
            case SpriteTool::Fill:
                pushColorHistory(fgColor_);
                applyFill(pixX, pixY);
                break;
            case SpriteTool::Eyedropper:
                fgColor_     = getPixel(pixX, pixY);
                currentTool_ = prevTool_;   // restore
                break;
            case SpriteTool::Line:
            case SpriteTool::Rect:
            case SpriteTool::Select:
                dragging_ = true;
                dragX0_ = pixX; dragY0_ = pixY;
                break;
            }
        }

        // Mouse held → drag
        if (ImGui::IsMouseDown(0) && !ImGui::IsMouseClicked(0)) {
            switch (currentTool_) {
            case SpriteTool::Pencil:
                // Bresenham interpolation to fill gaps
                if (prevPixX_ >= 0)
                    applyLine(prevPixX_, prevPixY_, pixX, pixY, fgColor_);
                else
                    applyPencil(pixX, pixY);
                prevPixX_ = pixX; prevPixY_ = pixY;
                break;
            case SpriteTool::Eraser:
                if (prevPixX_ >= 0)
                    applyLine(prevPixX_, prevPixY_, pixX, pixY, 0u);
                else
                    applyEraser(pixX, pixY);
                prevPixX_ = pixX; prevPixY_ = pixY;
                break;
            default:
                break;
            }
        }

        // Mouse released
        if (ImGui::IsMouseReleased(0)) {
            if (dragging_) {
                bakeGeometryDrag(pixX, pixY);
                dragging_ = false;
            }
            prevPixX_ = prevPixY_ = -1;
        }
    } else {
        // Mouse left canvas — reset drag tracking
        if (!ImGui::IsMouseDown(0)) {
            prevPixX_ = prevPixY_ = -1;
        }
    }

    ImGui::EndChild();
}

// ─── New canvas modal ─────────────────────────────────────────────────────────
void SpriteEditorPanel::drawNewModal()
{
    if (showNewModal_) {
        ImGui::OpenPopup("New Sprite##modal");
        showNewModal_ = false;
        newW_ = 16; newH_ = 16;
    }
    if (ImGui::BeginPopupModal("New Sprite##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static const int sizes[] = { 8, 16, 32, 48, 64 };
        static int selW = 1, selH = 1; // index into sizes[]
        ImGui::Text("Canvas size:");
        ImGui::SetNextItemWidth(80);
        if (ImGui::BeginCombo("W##nw", std::to_string(sizes[selW]).c_str())) {
            for (int i = 0; i < 5; ++i) {
                if (ImGui::Selectable(std::to_string(sizes[i]).c_str(), selW == i)) selW = i;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        if (ImGui::BeginCombo("H##nh", std::to_string(sizes[selH]).c_str())) {
            for (int i = 0; i < 5; ++i) {
                if (ImGui::Selectable(std::to_string(sizes[i]).c_str(), selH == i)) selH = i;
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Create", {80,0})) {
            newCanvas(sizes[selW], sizes[selH]);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {80,0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ─── Save modal ───────────────────────────────────────────────────────────────
void SpriteEditorPanel::drawSaveModal()
{
    if (showSaveModal_) {
        if (std::strncmp(saveNameBuf_, "sprite", sizeof(saveNameBuf_)) == 0) {
            std::snprintf(saveNameBuf_, sizeof(saveNameBuf_), "sprite_%dx%d", canvasW_, canvasH_);
        }
        ImGui::OpenPopup("Save Sprite##modal");
        showSaveModal_ = false;
    }
    if (ImGui::BeginPopupModal("Save Sprite##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Sprite name (saved to assets/sprites/):");
        ImGui::SetNextItemWidth(220);
        ImGui::InputText("##sname", saveNameBuf_, sizeof(saveNameBuf_));
        if (ImGui::Button("Save", {80,0})) {
            std::string outPath = AppPaths::getResourcesDir()
                + "/assets/sprites/" + saveNameBuf_ + ".png";
            // Convert ABGR→RGBA for stb
            std::vector<uint8_t> rgba(static_cast<size_t>(canvasW_ * canvasH_ * 4));
            for (int i = 0; i < canvasW_ * canvasH_; ++i) {
                uint32_t px = pixels_[static_cast<size_t>(i)];
                rgba[static_cast<size_t>(i*4+0)] = (px >>  0) & 0xFF; // R
                rgba[static_cast<size_t>(i*4+1)] = (px >>  8) & 0xFF; // G
                rgba[static_cast<size_t>(i*4+2)] = (px >> 16) & 0xFF; // B
                rgba[static_cast<size_t>(i*4+3)] = (px >> 24) & 0xFF; // A
            }
            if (stbi_write_png(outPath.c_str(), canvasW_, canvasH_, 4,
                                rgba.data(), canvasW_ * 4))
                currentPath = outPath;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {80,0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ─── Main draw ────────────────────────────────────────────────────────────────
void SpriteEditorPanel::draw()
{
    // Title shows filename + dirty marker
    std::string title = ICON_FA_PAINTBRUSH " Sprite Editor";
    if (!currentPath.empty()) {
        title += " — " + fs::path(currentPath).filename().string();
    }
    if (dirty_) title += " *";
    title += "##SEPanel";

    ImGui::SetNextWindowSize({720, 500}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(title.c_str(), &isOpen)) {
        ImGui::End();
        return;
    }

    // ── Top bar ──────────────────────────────────────────────────────────────
    if (ImGui::Button(ICON_FA_FILE " New"))        showNewModal_ = true;
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save")) {
        if (currentPath.empty()) showSaveModal_ = true;
        else {
            std::vector<uint8_t> rgba(static_cast<size_t>(canvasW_ * canvasH_ * 4));
            for (int i = 0; i < canvasW_ * canvasH_; ++i) {
                uint32_t px = pixels_[static_cast<size_t>(i)];
                rgba[static_cast<size_t>(i*4+0)] = (px >>  0) & 0xFF;
                rgba[static_cast<size_t>(i*4+1)] = (px >>  8) & 0xFF;
                rgba[static_cast<size_t>(i*4+2)] = (px >> 16) & 0xFF;
                rgba[static_cast<size_t>(i*4+3)] = (px >> 24) & 0xFF;
            }
            stbi_write_png(currentPath.c_str(), canvasW_, canvasH_, 4,
                           rgba.data(), canvasW_ * 4);
        }
    }
    ImGui::SameLine();
    // Zoom combo
    ImGui::SetNextItemWidth(70);
    static const float zoomLevels[] = {2.f, 4.f, 6.f, 8.f, 12.f, 16.f, 24.f, 32.f};
    static const char* zoomLabels[] = {"2×","4×","6×","8×","12×","16×","24×","32×"};
    int zIdx = 5;   // default 16×
    for (int i = 0; i < 8; ++i) if (std::fabs(zoom_ - zoomLevels[i]) < 0.1f) { zIdx = i; break; }
    if (ImGui::BeginCombo("Zoom", zoomLabels[zIdx])) {
        for (int i = 0; i < 8; ++i)
            if (ImGui::Selectable(zoomLabels[i], zIdx == i)) zoom_ = zoomLevels[i];
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("%d × %d px", canvasW_, canvasH_);

    ImGui::Separator();

    // ── Toolbar row ──────────────────────────────────────────────────────────
    drawToolbar();
    ImGui::SameLine(0, 16);
    drawColorSection();

    ImGui::Separator();

    // ── Canvas area ──────────────────────────────────────────────────────────
    drawCanvasArea();

    // ── Modals ───────────────────────────────────────────────────────────────
    drawNewModal();
    drawSaveModal();

    // ── Upload dirty pixels ───────────────────────────────────────────────────
    if (dirty_) uploadToGPU();

    ImGui::End();
}
