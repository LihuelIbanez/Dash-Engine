#include "SpriteEditorPanel.h"
#include "IconsFontAwesome6.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include "AppPaths.h"
#include "SceneData.h"
#include "CommandStack.h"
#include "AddComponentCommand.h"
#include "EditComponentFieldCommand.h"
#include "Reflection.h"
#include "World.h"
#include "TextureCache.h"
#include "IsoRenderer.h"
#include "SpriteOps.h"
#include <imgui.h>
#include <SDL2/SDL.h>
#include <cstring>
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

const char* SpriteEditorPanel::anchorToString(SpriteAnchor a)
{
    switch (a) {
        case SpriteAnchor::TopLeft:      return "TopLeft";
        case SpriteAnchor::TopCenter:    return "TopCenter";
        case SpriteAnchor::TopRight:     return "TopRight";
        case SpriteAnchor::MiddleLeft:   return "MiddleLeft";
        case SpriteAnchor::Center:       return "Center";
        case SpriteAnchor::MiddleRight:  return "MiddleRight";
        case SpriteAnchor::BottomLeft:   return "BottomLeft";
        case SpriteAnchor::BottomCenter: return "BottomCenter";
        case SpriteAnchor::BottomRight:  return "BottomRight";
        case SpriteAnchor::Custom:       return "Custom";
    }
    return "BottomCenter";
}

SpriteAnchor SpriteEditorPanel::anchorFromString(const std::string& s)
{
    if (s == "TopLeft")      return SpriteAnchor::TopLeft;
    if (s == "TopCenter")    return SpriteAnchor::TopCenter;
    if (s == "TopRight")     return SpriteAnchor::TopRight;
    if (s == "MiddleLeft")   return SpriteAnchor::MiddleLeft;
    if (s == "Center")       return SpriteAnchor::Center;
    if (s == "MiddleRight")  return SpriteAnchor::MiddleRight;
    if (s == "BottomLeft")   return SpriteAnchor::BottomLeft;
    if (s == "BottomCenter") return SpriteAnchor::BottomCenter;
    if (s == "BottomRight")  return SpriteAnchor::BottomRight;
    if (s == "Custom")       return SpriteAnchor::Custom;
    return SpriteAnchor::BottomCenter;
}

void SpriteEditorPanel::setPivotFromAnchor(SpriteAnchor a)
{
    switch (a) {
        case SpriteAnchor::TopLeft:      pivotX_ = 0.f;  pivotY_ = 0.f;  break;
        case SpriteAnchor::TopCenter:    pivotX_ = 0.5f; pivotY_ = 0.f;  break;
        case SpriteAnchor::TopRight:     pivotX_ = 1.f;  pivotY_ = 0.f;  break;
        case SpriteAnchor::MiddleLeft:   pivotX_ = 0.f;  pivotY_ = 0.5f; break;
        case SpriteAnchor::Center:       pivotX_ = 0.5f; pivotY_ = 0.5f; break;
        case SpriteAnchor::MiddleRight:  pivotX_ = 1.f;  pivotY_ = 0.5f; break;
        case SpriteAnchor::BottomLeft:   pivotX_ = 0.f;  pivotY_ = 1.f;  break;
        case SpriteAnchor::BottomCenter: pivotX_ = 0.5f; pivotY_ = 1.f;  break;
        case SpriteAnchor::BottomRight:  pivotX_ = 1.f;  pivotY_ = 1.f;  break;
        case SpriteAnchor::Custom: break;
    }
}

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
    const fs::path path = fs::path(AppPaths::getAssetsDir()) / "sprites" / "default_palette.json";
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
    const fs::path path = fs::path(AppPaths::getAssetsDir()) / "sprites" / "default_palette.json";
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
    composite_.assign(static_cast<size_t>(w * h), 0u);
    layers_.clear();
    SpriteLayer base;
    base.name = "Layer 1";
    base.pixels.assign(static_cast<size_t>(w * h), 0u);
    layers_.push_back(std::move(base));
    activeLayer_ = 0;
    renamingLayer_ = -1;
    selX_ = selY_ = -1; selW_ = selH_ = 0;
    prevPixX_ = prevPixY_ = -1;
    dragging_ = false;
    anchor_ = SpriteAnchor::BottomCenter;
    pivotX_ = 0.5f;
    pivotY_ = 1.0f;
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
        for (int y = 0; y < canvasH_; ++y) {
            auto* dst = reinterpret_cast<uint32_t*>(
                static_cast<uint8_t*>(ptr) + y * pitch);
            const auto* src = composite_.data() + y * canvasW_;
            std::memcpy(dst, src, static_cast<size_t>(canvasW_) * 4);
        }
        SDL_UnlockTexture(canvasTex_);
    }
    dirty_ = false;
}

// ─── Layer composition (D41) ──────────────────────────────────────────────────
// Porter-Duff src-over in ABGR8888, with per-layer opacity pre-multiplied into alpha.
uint32_t SpriteEditorPanel::alphaOver(uint32_t dst, uint32_t src, float opacity)
{
    return SpriteOps::alphaOver(dst, src, opacity);
}

void SpriteEditorPanel::compositeLayers()
{
    std::fill(composite_.begin(), composite_.end(), 0u);
    for (const auto& layer : layers_) {
        if (!layer.visible) continue;
        const int count = canvasW_ * canvasH_;
        for (int i = 0; i < count; ++i)
            composite_[static_cast<size_t>(i)] =
                alphaOver(composite_[static_cast<size_t>(i)],
                          layer.pixels[static_cast<size_t>(i)],
                          layer.opacity);
    }
    dirty_ = true;
}

// ─── Layer management (D41) ───────────────────────────────────────────────────
void SpriteEditorPanel::addLayer()
{
    if (static_cast<int>(layers_.size()) >= 8) return;
    SpriteLayer nl;
    nl.name = "Layer " + std::to_string(layers_.size() + 1);
    nl.pixels.assign(static_cast<size_t>(canvasW_ * canvasH_), 0u);
    // insert above active
    int insertPos = activeLayer_ + 1;
    layers_.insert(layers_.begin() + insertPos, std::move(nl));
    activeLayer_ = insertPos;
    compositeLayers();
}

void SpriteEditorPanel::deleteActiveLayer()
{
    if (layers_.size() <= 1) return;
    layers_.erase(layers_.begin() + activeLayer_);
    if (activeLayer_ >= static_cast<int>(layers_.size()))
        activeLayer_ = static_cast<int>(layers_.size()) - 1;
    compositeLayers();
}

void SpriteEditorPanel::moveLayerUp()
{
    if (activeLayer_ + 1 >= static_cast<int>(layers_.size())) return;
    std::swap(layers_[static_cast<size_t>(activeLayer_)],
              layers_[static_cast<size_t>(activeLayer_ + 1)]);
    ++activeLayer_;
    compositeLayers();
}

void SpriteEditorPanel::moveLayerDown()
{
    if (activeLayer_ <= 0) return;
    std::swap(layers_[static_cast<size_t>(activeLayer_)],
              layers_[static_cast<size_t>(activeLayer_ - 1)]);
    --activeLayer_;
    compositeLayers();
}

void SpriteEditorPanel::mergeLayerDown()
{
    if (activeLayer_ <= 0 || layers_.size() < 2) return;
    auto& top = layers_[static_cast<size_t>(activeLayer_)];
    auto& bot = layers_[static_cast<size_t>(activeLayer_ - 1)];
    const int count = canvasW_ * canvasH_;
    for (int i = 0; i < count; ++i)
        bot.pixels[static_cast<size_t>(i)] =
            alphaOver(bot.pixels[static_cast<size_t>(i)],
                      top.pixels[static_cast<size_t>(i)],
                      top.opacity);
    bot.opacity = std::max(bot.opacity, top.opacity);
    layers_.erase(layers_.begin() + activeLayer_);
    --activeLayer_;
    compositeLayers();
}

void SpriteEditorPanel::flattenAll()
{
    compositeLayers();
    layers_.clear();
    SpriteLayer flat;
    flat.name   = "Layer 1";
    flat.pixels = composite_;
    layers_.push_back(std::move(flat));
    activeLayer_ = 0;
    compositeLayers();
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

// ─── Pixel primitives (operate on ACTIVE LAYER) ──────────────────────────────
void SpriteEditorPanel::setPixel(int x, int y, uint32_t color)
{
    if (!inBounds(x, y)) return;
    if (activeLayer_ < 0 || activeLayer_ >= static_cast<int>(layers_.size())) return;
    layers_[static_cast<size_t>(activeLayer_)].pixels[static_cast<size_t>(y * canvasW_ + x)] = color;
    dirty_ = true;
}

uint32_t SpriteEditorPanel::getPixel(int x, int y) const
{
    if (!inBounds(x, y)) return 0u;
    if (activeLayer_ < 0 || activeLayer_ >= static_cast<int>(layers_.size())) return 0u;
    return layers_[static_cast<size_t>(activeLayer_)].pixels[static_cast<size_t>(y * canvasW_ + x)];
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
    if (activeLayer_ < 0 || activeLayer_ >= static_cast<int>(layers_.size())) return;
    SpriteOps::floodFill(layers_[static_cast<size_t>(activeLayer_)].pixels,
                         canvasW_, canvasH_, x, y, fgColor_);
    dirty_ = true;
}

void SpriteEditorPanel::applyLine(int x0, int y0, int x1, int y1, uint32_t color)
{
    if (activeLayer_ < 0 || activeLayer_ >= static_cast<int>(layers_.size())) return;
    auto& dst = layers_[static_cast<size_t>(activeLayer_)].pixels;
    for (int dy = 0; dy < brushSize_; ++dy)
        for (int dx = 0; dx < brushSize_; ++dx)
            SpriteOps::drawLine(dst, canvasW_, canvasH_,
                                x0 + dx, y0 + dy, x1 + dx, y1 + dy, color);
    dirty_ = true;
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
    auto toolBtn = [&](const char* label, SpriteTool t, const char* tip) {
        bool active = (currentTool_ == t);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label, {36, 32})) {
            prevTool_    = currentTool_;
            currentTool_ = t;
        }
        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
    };

    toolBtn(ICON_FA_PENCIL,       SpriteTool::Pencil,     "Pencil (P)");
    toolBtn(ICON_FA_ERASER,       SpriteTool::Eraser,     "Eraser (E)");
    toolBtn(ICON_FA_FILL_DRIP,    SpriteTool::Fill,       "Fill (G)");
    toolBtn(ICON_FA_EYE_DROPPER,  SpriteTool::Eyedropper, "Eyedropper (I)");
    toolBtn(ICON_FA_MINUS,        SpriteTool::Line,       "Line (L)");
    toolBtn(ICON_FA_SQUARE,       SpriteTool::Rect,       "Rectangle (R)");
    toolBtn(ICON_FA_OBJECT_GROUP, SpriteTool::Select,     "Select (S)");

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
            if (ImGui::IsKeyPressed(ImGuiKey_X, false)) std::swap(fgColor_, bgColor_);
            if (ImGui::IsKeyPressed(ImGuiKey_D, false)) {
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

    // Draw checkerboard under the sprite (procedural; robust on all backends)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float tile = 8.f;
        const ImU32 c0 = IM_COL32(210, 210, 210, 255);
        const ImU32 c1 = IM_COL32(160, 160, 160, 255);
        for (float y = 0.f; y < ch; y += tile) {
            for (float x = 0.f; x < cw; x += tile) {
                int tx = static_cast<int>(x / tile);
                int ty = static_cast<int>(y / tile);
                ImU32 c = ((tx + ty) & 1) ? c1 : c0;
                ImVec2 p0 = {origin.x + x, origin.y + y};
                ImVec2 p1 = {std::min(origin.x + x + tile, origin.x + cw),
                             std::min(origin.y + y + tile, origin.y + ch)};
                dl->AddRectFilled(p0, p1, c);
            }
        }
        dl->AddRect(origin, {origin.x + cw, origin.y + ch}, IM_COL32(40, 40, 40, 220), 0.f, 0, 1.0f);
    }

    // Sprite image
    ImGui::Image(reinterpret_cast<ImTextureID>(canvasTex_), {cw, ch});
    bool hovered = ImGui::IsItemHovered();
    if (hovered) {
        switch (currentTool_) {
            case SpriteTool::Pencil:
            case SpriteTool::Eraser:
            case SpriteTool::Fill:
            case SpriteTool::Eyedropper:
            case SpriteTool::Line:
            case SpriteTool::Rect:
                ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
                break;
            case SpriteTool::Select:
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                break;
        }
    }

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
            if (ImGui::IsKeyPressed(ImGuiKey_P, false)) currentTool_ = SpriteTool::Pencil;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) currentTool_ = SpriteTool::Eraser;
            if (ImGui::IsKeyPressed(ImGuiKey_G, false)) currentTool_ = SpriteTool::Fill;
            if (ImGui::IsKeyPressed(ImGuiKey_I, false)) currentTool_ = SpriteTool::Eyedropper;
            if (ImGui::IsKeyPressed(ImGuiKey_L, false)) currentTool_ = SpriteTool::Line;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) currentTool_ = SpriteTool::Rect;
            if (ImGui::IsKeyPressed(ImGuiKey_S, false) && !ImGui::GetIO().KeyCtrl) currentTool_ = SpriteTool::Select;

            // Ctrl+C / Ctrl+V on selection
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && selX_ >= 0) {
                clipW_ = selW_; clipH_ = selH_;
                clipboard_.resize(static_cast<size_t>(clipW_ * clipH_));
                for (int y = 0; y < clipH_; ++y)
                    for (int x = 0; x < clipW_; ++x)
                        clipboard_[static_cast<size_t>(y * clipW_ + x)] = getPixel(selX_ + x, selY_ + y);
            }
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !clipboard_.empty()) {
                for (int y = 0; y < clipH_; ++y)
                    for (int x = 0; x < clipW_; ++x)
                        setPixel(pixX + x, pixY + y,
                            clipboard_[static_cast<size_t>(y * clipW_ + x)]);
                dirty_ = true;
            }
            // Delete selection
            if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && selX_ >= 0) {
                for (int y = selY_; y < selY_ + selH_; ++y)
                    for (int x = selX_; x < selX_ + selW_; ++x)
                        setPixel(x, y, 0u);
                dirty_ = true;
            }
            // Escape = deselect
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { selX_ = selY_ = -1; selW_ = selH_ = 0; }
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

    } else {
        // Mouse left canvas — reset drag tracking
        if (!ImGui::IsMouseDown(0)) {
            prevPixX_ = prevPixY_ = -1;
        }
    }

    // Finalize geometry drags on release even if mouse exits the canvas area.
    if (ImGui::IsMouseReleased(0)) {
        if (dragging_) {
            bakeGeometryDrag(pixX, pixY);
            dragging_ = false;
        }
        prevPixX_ = prevPixY_ = -1;
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
            std::string outPath = AppPaths::getAssetsDir()
                + "/sprites/" + saveNameBuf_ + ".png";
            if (saveAsPNG(outPath)) {
                currentPath = outPath;
                TextureCache::instance().invalidate(renderer_, outPath);
                if (pendingCloseAfterSave_) {
                    pendingCloseAfterSave_ = false;
                    dirty_ = false;
                    isOpen = false;
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {80,0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void SpriteEditorPanel::drawCloseConfirmModal()
{
    if (showCloseConfirm_) {
        ImGui::OpenPopup("Unsaved Sprite##closeConfirm");
        showCloseConfirm_ = false;
    }
    if (ImGui::BeginPopupModal("Unsaved Sprite##closeConfirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Save changes before closing Sprite Editor?");
        ImGui::Spacing();

        if (ImGui::Button("Save", {90, 0})) {
            if (!currentPath.empty()) {
                if (saveAsPNG(currentPath)) {
                    TextureCache::instance().invalidate(renderer_, currentPath);
                    dirty_ = false;
                    isOpen = false;
                    pendingCloseAfterSave_ = false;
                }
            } else {
                showSaveModal_ = true;
                pendingCloseAfterSave_ = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", {90, 0})) {
            dirty_ = false;
            pendingCloseAfterSave_ = false;
            isOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {90, 0})) {
            pendingCloseAfterSave_ = false;
            isOpen = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// ─── Save / Load helpers (D42) ───────────────────────────────────────────────
bool SpriteEditorPanel::saveAsPNG(const std::string& path)
{
    compositeLayers();
    std::vector<uint8_t> rgba(static_cast<size_t>(canvasW_ * canvasH_ * 4));
    for (int i = 0; i < canvasW_ * canvasH_; ++i) {
        uint32_t px = composite_[static_cast<size_t>(i)];
        rgba[static_cast<size_t>(i*4+0)] = (px >>  0) & 0xFF;  // R
        rgba[static_cast<size_t>(i*4+1)] = (px >>  8) & 0xFF;  // G
        rgba[static_cast<size_t>(i*4+2)] = (px >> 16) & 0xFF;  // B
        rgba[static_cast<size_t>(i*4+3)] = (px >> 24) & 0xFF;  // A
    }
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    bool pngOk = stbi_write_png(path.c_str(), canvasW_, canvasH_, 4,
                                rgba.data(), canvasW_ * 4) != 0;
    if (!pngOk) return false;
    return saveSpriteMeta(path);
}

bool SpriteEditorPanel::saveSpriteMeta(const std::string& pngPath) const
{
    fs::path metaPath = fs::path(pngPath).replace_extension(".sprite.json");
    std::error_code ec;
    fs::create_directories(metaPath.parent_path(), ec);

    json j;
    j["anchor"] = anchorToString(anchor_);
    j["pivotX"] = pivotX_;
    j["pivotY"] = pivotY_;

    std::ofstream out(metaPath);
    if (!out) return false;
    out << j.dump(2);
    return true;
}

void SpriteEditorPanel::loadSpriteMeta(const std::string& pngPath)
{
    anchor_ = SpriteAnchor::BottomCenter;
    pivotX_ = 0.5f;
    pivotY_ = 1.0f;

    fs::path metaPath = fs::path(pngPath).replace_extension(".sprite.json");
    std::ifstream in(metaPath);
    if (!in) return;

    try {
        json j;
        in >> j;
        if (j.contains("anchor") && j["anchor"].is_string())
            anchor_ = anchorFromString(j["anchor"].get<std::string>());
        if (j.contains("pivotX") && j["pivotX"].is_number())
            pivotX_ = j["pivotX"].get<float>();
        if (j.contains("pivotY") && j["pivotY"].is_number())
            pivotY_ = j["pivotY"].get<float>();
    } catch (...) {
        // Keep defaults if metadata is invalid.
    }

    pivotX_ = std::clamp(pivotX_, 0.f, 1.f);
    pivotY_ = std::clamp(pivotY_, 0.f, 1.f);
}

bool SpriteEditorPanel::loadFromPNG(const std::string& path)
{
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);  // force RGBA
    if (!data) return false;

    canvasW_ = w;
    canvasH_ = h;
    composite_.assign(static_cast<size_t>(w * h), 0u);

    layers_.clear();
    SpriteLayer base;
    base.name = fs::path(path).stem().string();
    base.pixels.resize(static_cast<size_t>(w * h));

    // stb_image returns RGBA; SDL texture is ABGR8888 → repack
    for (int i = 0; i < w * h; ++i) {
        uint32_t r = data[i*4+0], g = data[i*4+1],
                 b = data[i*4+2], a = data[i*4+3];
        base.pixels[static_cast<size_t>(i)] =
            (a << 24) | (b << 16) | (g << 8) | r;
    }
    stbi_image_free(data);
    layers_.push_back(std::move(base));
    activeLayer_ = 0;
    renamingLayer_ = -1;
    selX_ = selY_ = -1; selW_ = selH_ = 0;
    prevPixX_ = prevPixY_ = -1;
    dragging_ = false;
    currentPath = path;
    loadSpriteMeta(path);
    rebuildTexture();
    compositeLayers();
    return true;
}

void SpriteEditorPanel::drawIsoPreviewPanel()
{
    if (!ImGui::CollapsingHeader("Iso Preview", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextDisabled("Anchor");
    static const struct { SpriteAnchor a; const char* label; } kAnchors[] = {
        {SpriteAnchor::TopLeft, "TL"}, {SpriteAnchor::TopCenter, "TC"}, {SpriteAnchor::TopRight, "TR"},
        {SpriteAnchor::MiddleLeft, "ML"}, {SpriteAnchor::Center, "C"}, {SpriteAnchor::MiddleRight, "MR"},
        {SpriteAnchor::BottomLeft, "BL"}, {SpriteAnchor::BottomCenter, "BC"}, {SpriteAnchor::BottomRight, "BR"},
    };

    for (int i = 0; i < 9; ++i) {
        bool selected = (anchor_ == kAnchors[i].a);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(kAnchors[i].label, {30, 24})) {
            anchor_ = kAnchors[i].a;
            setPivotFromAnchor(anchor_);
        }
        if (selected) ImGui::PopStyleColor();
        if ((i % 3) != 2) ImGui::SameLine();
    }

    ImGui::SameLine(0, 12);
    bool custom = (anchor_ == SpriteAnchor::Custom);
    if (custom) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button("Custom", {72, 24})) anchor_ = SpriteAnchor::Custom;
    if (custom) ImGui::PopStyleColor();

    if (anchor_ == SpriteAnchor::Custom) {
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputFloat2("Pivot", &pivotX_)) {
            pivotX_ = std::clamp(pivotX_, 0.f, 1.f);
            pivotY_ = std::clamp(pivotY_, 0.f, 1.f);
        }
    } else {
        ImGui::TextDisabled("Pivot: (%.2f, %.2f)", pivotX_, pivotY_);
    }

    if (ImGui::Button("Snap to tile bottom")) {
        anchor_ = SpriteAnchor::BottomCenter;
        setPivotFromAnchor(anchor_);
    }

    const char* bgItems[] = {"Checker", "Black", "White"};
    int bg = static_cast<int>(previewBg_);
    ImGui::SetNextItemWidth(120);
    if (ImGui::Combo("Background", &bg, bgItems, 3)) {
        previewBg_ = static_cast<PreviewBg>(bg);
    }

    auto drawIsoPreview = [&](const char* id, float scale) {
        ImGui::Text("%s", id);
        ImVec2 size = {180.f, 140.f};
        ImGui::BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = {p0.x + size.x - 8.f, p0.y + size.y - 8.f};

        if (previewBg_ == PreviewBg::Checker && checkerTex_) {
            dl->AddImage(reinterpret_cast<ImTextureID>(checkerTex_), p0, p1,
                         {0, 0}, {(p1.x - p0.x) / 16.f, (p1.y - p0.y) / 16.f});
        } else {
            ImU32 bgCol = (previewBg_ == PreviewBg::Black)
                        ? IM_COL32(20, 20, 20, 255)
                        : IM_COL32(240, 240, 240, 255);
            dl->AddRectFilled(p0, p1, bgCol);
        }

        const float cx = (p0.x + p1.x) * 0.5f;
        const float cy = (p0.y + p1.y) * 0.62f;
        const float hw = (TILE_W * 0.5f) * scale;
        const float hh = (TILE_H * 0.5f) * scale;

        ImVec2 poly[4] = {
            {cx,      cy - hh},
            {cx + hw, cy},
            {cx,      cy + hh},
            {cx - hw, cy},
        };
        dl->AddConvexPolyFilled(poly, 4, IM_COL32(88, 104, 84, 220));
        dl->AddPolyline(poly, 4, IM_COL32(200, 220, 200, 220), ImDrawFlags_Closed, 1.5f);

        if (canvasTex_) {
            float sw = canvasW_ * scale;
            float sh = canvasH_ * scale;
            float x0 = cx - pivotX_ * sw;
            float y0 = cy - pivotY_ * sh;
            dl->AddImage(reinterpret_cast<ImTextureID>(canvasTex_),
                        {x0, y0}, {x0 + sw, y0 + sh});
            dl->AddCircleFilled({cx, cy}, 2.5f, IM_COL32(255, 80, 80, 255));
        }

        ImGui::EndChild();
    };

    drawIsoPreview("2x", 2.f);
    ImGui::SameLine();
    drawIsoPreview("4x", 4.f);
}

// ─── Open modal (D42) ────────────────────────────────────────────────────────
void SpriteEditorPanel::drawOpenModal()
{
    if (showOpenModal_) {
        ImGui::OpenPopup("Open Sprite##modal");
        showOpenModal_ = false;
        // Enumerate sprites/
        spriteFiles_.clear();
        selectedSpriteIdx_ = -1;
        std::string spritesDir;
        if (assetsRoot)
            spritesDir = *assetsRoot + "/sprites";
        else
            spritesDir = AppPaths::getAssetsDir() + "/sprites";

        std::error_code ec;
        for (auto& entry : fs::directory_iterator(spritesDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".png")
                spriteFiles_.push_back(entry.path().string());
        }
        std::sort(spriteFiles_.begin(), spriteFiles_.end());
    }

    if (ImGui::BeginPopupModal("Open Sprite##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Select a sprite from assets/sprites/:");
        ImGui::SetNextItemWidth(320);

        if (ImGui::BeginListBox("##spritelist", {320, 200})) {
            for (int i = 0; i < static_cast<int>(spriteFiles_.size()); ++i) {
                std::string label = fs::path(spriteFiles_[static_cast<size_t>(i)]).filename().string();
                if (ImGui::Selectable(label.c_str(), selectedSpriteIdx_ == i))
                    selectedSpriteIdx_ = i;
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    // Double-click = open directly
                    loadFromPNG(spriteFiles_[static_cast<size_t>(i)]);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndListBox();
        }

        bool canOpen = selectedSpriteIdx_ >= 0
                       && selectedSpriteIdx_ < static_cast<int>(spriteFiles_.size());
        if (!canOpen) ImGui::BeginDisabled();
        if (ImGui::Button("Open", {80, 0})) {
            loadFromPNG(spriteFiles_[static_cast<size_t>(selectedSpriteIdx_)]);
            ImGui::CloseCurrentPopup();
        }
        if (!canOpen) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {80, 0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ─── Assign to entity panel (D42) ────────────────────────────────────────────
void SpriteEditorPanel::drawAssignPanel()
{
    if (!showAssignPanel_) return;

    ImGui::SetNextWindowSize({320, 240}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_LINK " Assign Sprite##assign", &showAssignPanel_)) {
        ImGui::End(); return;
    }

    // Need a saved sprite and a selected entity
    if (currentPath.empty()) {
        ImGui::TextDisabled("Save the sprite first, then assign.");
        ImGui::End(); return;
    }
    if (!selectedEntityId || *selectedEntityId == 0) {
        ImGui::TextDisabled("Select an entity in the Hierarchy first.");
        ImGui::End(); return;
    }
    if (!scene || !commandStack || !world) {
        ImGui::TextDisabled("Editor context not connected.");
        ImGui::End(); return;
    }

    std::string spriteName = fs::path(currentPath).stem().string();
    ImGui::Text("Sprite:  %s", spriteName.c_str());
    ImGui::Separator();

    // Find selected entity
    EntityData* ent = nullptr;
    for (auto& e : scene->entities)
        if (e.id == *selectedEntityId) { ent = &e; break; }

    if (!ent) {
        ImGui::TextDisabled("Entity #%llu not found in scene.",
                            static_cast<unsigned long long>(*selectedEntityId));
        ImGui::End(); return;
    }

    ImGui::Text("Entity:  %s  [id=%llu]",
                ent->name.c_str(),
                static_cast<unsigned long long>(ent->id));

    // Check for existing RenderComponent
    bool hasRender = false;
    std::string currentSprite;
    for (auto& comp : ent->components) {
        if (getVariantType(comp) == ComponentType::Render) {
            hasRender      = true;
            currentSprite  = std::get<RenderComponent>(comp).sprite;
            break;
        }
    }
    if (hasRender)
        ImGui::TextDisabled("Current sprite: \"%s\"", currentSprite.c_str());
    else
        ImGui::TextColored({1.f, 0.6f, 0.f, 1.f}, "No RenderComponent (will be added)");

    ImGui::Spacing();
    if (ImGui::Button("Assign##exec", {140, 0})) {
        // 1. Add RenderComponent if missing
        if (!hasRender) {
            commandStack->execute(
                std::make_unique<AddComponentCommand>(*selectedEntityId, RenderComponent{}),
                *scene, *world);
        }

        // 2. Re-find entity (scene may have been modified by AddComponentCommand)
        EntityData* ent2 = nullptr;
        for (auto& e : scene->entities)
            if (e.id == *selectedEntityId) { ent2 = &e; break; }

        if (ent2) {
            // 3. Get sprite field offset via reflection
            const auto& meta = getComponentMeta(ComponentType::Render);
            for (const auto& prop : meta.properties) {
                if (prop.name != "sprite") continue;

                // Read old value from entity (now guaranteed to have RenderComponent)
                PropertyValue oldVal = std::string("default");
                for (auto& comp : ent2->components) {
                    if (getVariantType(comp) != ComponentType::Render) continue;
                    void* ptr = std::visit([off = prop.offset](auto& c) -> void* {
                        return reinterpret_cast<char*>(&c) + off;
                    }, comp);
                    oldVal = readFieldValue(ptr, PropertyType::String);
                    break;
                }

                // 4. Push edit command
                commandStack->execute(
                    std::make_unique<EditComponentFieldCommand>(
                        *selectedEntityId, ComponentType::Render,
                        prop.offset, PropertyType::String,
                        oldVal, std::string(spriteName), "sprite"),
                    *scene, *world);
                break;
            }
        }

        // 5. Invalidate texture cache so viewport reloads the sprite
        TextureCache::instance().invalidate(renderer_, currentPath);
    }

    ImGui::End();
}

// ─── Layers panel (D41) ───────────────────────────────────────────────────────
void SpriteEditorPanel::drawLayersPanel()
{
    if (!ImGui::CollapsingHeader(ICON_FA_LAYER_GROUP " Layers", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::BeginChild("##layers_inline", {0, 220}, true, ImGuiWindowFlags_NoScrollbar);
    // Action buttons
    bool canAdd    = static_cast<int>(layers_.size()) < 8;
    bool canDelete = layers_.size() > 1;
    bool canUp     = activeLayer_ + 1 < static_cast<int>(layers_.size());
    bool canDown   = activeLayer_ > 0;
    bool canMerge  = activeLayer_ > 0;

    if (!canAdd) ImGui::BeginDisabled();
    if (ImGui::SmallButton("+##addlayer"))   addLayer();
    if (!canAdd) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!canDelete) ImGui::BeginDisabled();
    if (ImGui::SmallButton("-##dellayer"))   deleteActiveLayer();
    if (!canDelete) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!canUp) ImGui::BeginDisabled();
    if (ImGui::SmallButton("^##moveup"))     moveLayerUp();
    if (!canUp) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!canDown) ImGui::BeginDisabled();
    if (ImGui::SmallButton("v##movedown"))   moveLayerDown();
    if (!canDown) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!canMerge) ImGui::BeginDisabled();
    if (ImGui::SmallButton("M##merge"))      mergeLayerDown();
    if (!canMerge) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("F##flatten"))    flattenAll();

    ImGui::Separator();

    // Layer list (top of vector = top of stack — draw reversed so topmost is first)
    for (int i = static_cast<int>(layers_.size()) - 1; i >= 0; --i) {
        auto& layer = layers_[static_cast<size_t>(i)];
        bool  isActive = (i == activeLayer_);

        // Visibility toggle
        ImGui::PushID(i);
        bool vis = layer.visible;
        if (ImGui::Checkbox("##vis", &vis)) {
            layer.visible = vis;
            compositeLayers();
        }
        ImGui::SameLine();

        // Selectable row — click to set active layer
        if (ImGui::Selectable(layer.name.c_str(), isActive,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
            activeLayer_ = i;
            if (ImGui::IsMouseDoubleClicked(0)) {
                renamingLayer_ = i;
                std::snprintf(renameLayerBuf_, sizeof(renameLayerBuf_),
                              "%s", layer.name.c_str());
            }
        }

        // Inline rename
        if (renamingLayer_ == i) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##rename", renameLayerBuf_, sizeof(renameLayerBuf_),
                                 ImGuiInputTextFlags_EnterReturnsTrue
                                 | ImGuiInputTextFlags_AutoSelectAll)) {
                layer.name      = renameLayerBuf_;
                renamingLayer_  = -1;
            }
            if (!ImGui::IsItemActive() && !ImGui::IsItemHovered())
                renamingLayer_ = -1;
        }

        ImGui::SameLine();
        // Opacity slider (shows float 0-1 as 0-100%)
        float op = layer.opacity * 100.f;
        ImGui::SetNextItemWidth(60);
        if (ImGui::SliderFloat("##op", &op, 0.f, 100.f, "%.0f%%")) {
            layer.opacity = op / 100.f;
            compositeLayers();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
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
        if (!isOpen && dirty_) {
            isOpen = true;
            showCloseConfirm_ = true;
        }
        drawCloseConfirmModal();
        return;
    }

    if (!isOpen && dirty_) {
        isOpen = true;
        showCloseConfirm_ = true;
    }

    // ── Top bar ──────────────────────────────────────────────────────────────
    if (ImGui::Button(ICON_FA_FILE " New"))        showNewModal_ = true;
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " Open")) showOpenModal_ = true;
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save")) {
        if (currentPath.empty()) {
            showSaveModal_ = true;
        } else {
            if (saveAsPNG(currentPath))
                TextureCache::instance().invalidate(renderer_, currentPath);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_LINK " Assign")) showAssignPanel_ = !showAssignPanel_;
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
    if (canvasTex_) {
        ImGui::SameLine(0, 16);
        ImGui::Image(reinterpret_cast<ImTextureID>(canvasTex_), {64, 64});
    }

    ImGui::Separator();

    // ── Toolbar row ──────────────────────────────────────────────────────────
    drawToolbar();
    ImGui::SameLine(0, 16);
    drawColorSection();

    ImGui::Separator();

    // ── Canvas area ──────────────────────────────────────────────────────────
    drawCanvasArea();

    ImGui::Separator();
    drawIsoPreviewPanel();

    // ── Layers panel ─────────────────────────────────────────────────────────
    drawLayersPanel();

    // ── Modals ───────────────────────────────────────────────────────────────
    drawNewModal();
    drawSaveModal();
    drawOpenModal();       // D42
    drawAssignPanel();     // D42
    drawCloseConfirmModal(); // D45

    // ── Composite + upload ────────────────────────────────────────────────────
    if (dirty_) { compositeLayers(); uploadToGPU(); }

    ImGui::End();
}
