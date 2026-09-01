// ═════════════════════════════════════════════════════════════════════════════
// EditorApp — Viewport toolbar, viewport interaction (camera, selection,
// prefab drop) and the terrain/entity tool dispatch (paint, brushes, water).
//
// Split out of EditorApp.cpp to keep that file navigable.
// ═════════════════════════════════════════════════════════════════════════════
#include "EditorApp.h"
#include "CliffBrushCommand.h"
#include "EraseCommand.h"
#include "FloodFillCommand.h"
#include "HeightBrushCommand.h"
#include "IconsFontAwesome6.h"
#include "MoveEntityCommand.h"
#include "PaintTileCommand.h"
#include "PlaceEnemyCommand.h"
#include "PlacePrefabCommand.h"
#include "PrefabAsset.h"
#include "TexturePaintCommand.h"
#include "WaterLevelCommand.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ═════════════════════════════════════════════════════════════════════════════
// Viewport
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawViewportToolbar()
{
    // Transform mode and snapping belong over the image they act on, not in a
    // menu: this is where the eye already is while manipulating an entity.
    constexpr float kBarHeight = 32.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6.0f, 4.0f});
    ImGui::BeginChild("##ViewportToolbar", {0.0f, kBarHeight},
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    const auto modeBtn = [&](const char* label, dash::gizmo::Mode m, const char* tip) {
        const bool selected = (gizmo_.mode() == m);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, {0.035f, 0.278f, 0.443f, 1.f});
        if (ImGui::Button(label, {36, 24})) gizmo_.setMode(m);
        if (selected) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
    };

    modeBtn(ICON_FA_UP_DOWN_LEFT_RIGHT, dash::gizmo::Mode::Translate, "Move (W)");
    modeBtn(ICON_FA_ROTATE,             dash::gizmo::Mode::Rotate,    "Rotate (E)");
    modeBtn(ICON_FA_MAXIMIZE,           dash::gizmo::Mode::Scale,     "Scale (R)");

    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    ImGui::Checkbox("Snap", &gizmoAlwaysSnap_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Always snap; hold Shift to snap while it is off");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(80);
    switch (gizmo_.mode()) {
        case dash::gizmo::Mode::Rotate:
            ImGui::DragFloat("##snaprot", &gizmoRotateSnapDeg_, 1.0f, 1.0f, 90.0f, "%.0f°");
            break;
        case dash::gizmo::Mode::Scale:
            ImGui::DragFloat("##snapscale", &gizmoScaleSnap_, 0.01f, 0.01f, 1.0f, "%.2f");
            break;
        default:
            ImGui::DragFloat("##snapmove", &gizmoTranslateSnapTiles_, 0.05f, 0.05f, 4.0f, "%.2f t");
            break;
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    ImGui::TextDisabled("%zu selected", selection_.size());

    ImGui::EndChild();
}

void EditorApp::drawViewport()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("Viewport");

    drawViewportToolbar();

    // Get viewport coordinates and size BEFORE rendering
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1) avail.x = 1;
    if (avail.y < 1) avail.y = 1;
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();

    // Render the world to the offscreen Vulkan texture (done in main loop after beginFrame)

    // Use full available area (Vulkan viewport auto-resizes)
    ImVec2 imgSize = avail;

    // Fill background with dark color
    ImDrawList* bgDl = ImGui::GetWindowDrawList();
    bgDl->AddRectFilled(cursorPos, {cursorPos.x + avail.x, cursorPos.y + avail.y}, IM_COL32(30, 30, 30, 255));

    // Display the Vulkan-rendered viewport texture
    ImGui::Image(vkCtx_.viewportTexture(), imgSize);

    // Update viewport mapping for mouse coordinate conversion
    vpDisplayW_ = imgSize.x;
    vpDisplayH_ = imgSize.y;
    vpScreenX_ = cursorPos.x;
    vpScreenY_ = cursorPos.y;

    // ── Prefab drag-drop target ──────────────────────────────────────────────
    if (editorMode_ == EditorMode::Edit && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PREFAB_GUID")) {
            std::string guid(static_cast<const char*>(payload->Data),
                             static_cast<size_t>(payload->DataSize) - 1);
            ImGuiIO& io = ImGui::GetIO();
            float mx = io.MousePos.x - vpScreenX_;
            float my = io.MousePos.y - vpScreenY_;
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
                    setSelection(newId);
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

    // ── Transform gizmo ──────────────────────────────────────────────────────
    // Runs before the tools below so a gizmo drag swallows the click instead of
    // painting a tile or moving the entity underneath.
    bool gizmoOwnsPointer = false;
    float gizmoViewProj[16];
    buildViewProjMatrix(vpDisplayW_, vpDisplayH_, gizmoViewProj);
    const dash::gizmo::ViewportRect gizmoRect{vpScreenX_, vpScreenY_, vpDisplayW_, vpDisplayH_};
    if (editorMode_ == EditorMode::Edit) {
        handleGizmoShortcuts(vpFocused);

        ImGuiIO& gio = ImGui::GetIO();
        gizmoOwnsPointer = updateViewportGizmo(gizmoViewProj, gizmoRect,
                                               gio.MousePos.x, gio.MousePos.y, vpHovered);

        drawSelectionOverlays(ImGui::GetWindowDrawList(), gizmoViewProj, gizmoRect);
        if (gizmoOwnsPointer) vpHovered = false;
    }

    // ── Play-mode input: Vulkan handles its own input ────────────────────────
    // (clicking in viewport sends input to Vulkan process, not editor)

    // ── Edit-mode interaction ────────────────────────────────────────────────

    // Edit mode: WASD pans camera
    if (vpFocused && editorMode_ == EditorMode::Edit) {
        ImGuiIO& io = ImGui::GetIO();
        float speed = 12.f * io.DeltaTime;
        if (ImGui::IsKeyDown(ImGuiKey_W)) { camX_ -= speed; camY_ -= speed; }
        if (ImGui::IsKeyDown(ImGuiKey_S)) { camX_ += speed; camY_ += speed; }
        if (ImGui::IsKeyDown(ImGuiKey_A)) { camX_ -= speed; camY_ += speed; }
        if (ImGui::IsKeyDown(ImGuiKey_D)) { camX_ += speed; camY_ -= speed; }
    }

    // Play mode: WASD moves player entity, camera follows
    if (vpFocused && editorMode_ == EditorMode::Play) {
        ImGuiIO& io = ImGui::GetIO();
        float speed = 6.f * io.DeltaTime;
        float dx = 0.f, dy = 0.f;
        if (ImGui::IsKeyDown(ImGuiKey_W)) { dx -= 1.f; dy -= 1.f; }
        if (ImGui::IsKeyDown(ImGuiKey_S)) { dx += 1.f; dy += 1.f; }
        if (ImGui::IsKeyDown(ImGuiKey_A)) { dx -= 1.f; dy += 1.f; }
        if (ImGui::IsKeyDown(ImGuiKey_D)) { dx += 1.f; dy -= 1.f; }

        if (dx != 0.f || dy != 0.f) {
            float len = std::sqrt(dx * dx + dy * dy);
            dx = dx / len * speed;
            dy = dy / len * speed;
            for (auto& e : scene_.entities) {
                if (e.type == EntityData::Type::Player) {
                    e.x += dx;
                    e.y += dy;
                    camX_ = e.x;
                    camY_ = e.y;
                    break;
                }
            }
        }
    }

    if (vpHovered) {
        // Change cursor based on active tool
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right))
            SDL_SetCursor(cursorMove_);
        else if (currentTool_ == Tool::PaintTile)
            SDL_SetCursor(cursorCrosshair_);
        else if (currentTool_ == Tool::FillTile)
            SDL_SetCursor(cursorCrosshair_);
        else if (currentTool_ == Tool::EyeDropper)
            SDL_SetCursor(cursorCrosshair_);
        else if (currentTool_ == Tool::HeightBrush)
            SDL_SetCursor(cursorCrosshair_);
        else if (currentTool_ == Tool::PlaceEnemy)
            SDL_SetCursor(cursorCrosshair_);
        else if (currentTool_ == Tool::Erase)
            SDL_SetCursor(cursorCrosshair_);
        else
            SDL_SetCursor(cursorHand_);

        ImGuiIO& io = ImGui::GetIO();

        // Scroll → zoom camera (change orbit distance)
        if (io.MouseWheel != 0.f) {
            viewport3D_.cameraDistance -= io.MouseWheel * 1.5f;
            viewport3D_.cameraDistance = std::clamp(viewport3D_.cameraDistance, 5.0f, 80.0f);
        }

        // Right-drag → pan camera
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
            camX_ -= d.x * 0.03f;
            camY_ -= d.y * 0.03f;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
        }

        float mx = io.MousePos.x - vpScreenX_;
        float my = io.MousePos.y - vpScreenY_;

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
            (currentTool_ == Tool::PaintTile || currentTool_ == Tool::HeightBrush) &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            float wx, wy;
            if (viewportScreenToWorld(mx, my, wx, wy)) {
                if (currentTool_ == Tool::PaintTile)
                    paintTileAt(wx, wy);
                else
                    heightBrushAt(wx, wy);
            }
        }
    } else {
        // Restore default arrow cursor outside viewport
        SDL_SetCursor(cursorArrow_);
    }

    // ── Marquee selection ────────────────────────────────────────────────────
    // After the tool handling above so an entity drag wins over the rectangle,
    // and outside the hover branch so a drag that leaves the image still ends.
    {
        const ImGuiIO& rio = ImGui::GetIO();
        updateRectSelection(gizmoViewProj, gizmoRect, rio.MousePos.x, rio.MousePos.y,
                            vpHovered, gizmoOwnsPointer);
    }

    // Play-mode overlay indicator
    if (editorMode_ == EditorMode::Play) {
        const std::string overlay = playback_.paused()
            ? "PAUSED " + playbackSpeedLabel()
            : "PLAYING " + playbackSpeedLabel();
        ImVec2 wp = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddRectFilled(
            {wp.x + 8, wp.y + 30}, {wp.x + 140, wp.y + 56},
            IM_COL32(200, 40, 40, 200), 4.f);
        ImGui::GetWindowDrawList()->AddText(
            {wp.x + 16, wp.y + 34}, IM_COL32(255, 255, 255, 255), overlay.c_str());
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

    case Tool::FillTile:
        floodFillAt(wx, wy);
        break;

    case Tool::EyeDropper: {
        int tx = (int)wx, ty = (int)wy;
        if (tx >= 0 && tx < WORLD_W && ty >= 0 && ty < WORLD_H) {
            selectedTileType_ = world_.terrain().face(tx, ty).type;
            currentTool_ = Tool::PaintTile;
        }
        break;
    }

    case Tool::HeightBrush: {
        heightBrushAt(wx, wy);
        break;
    }

    case Tool::CliffBrush: {
        cliffBrushAt(wx, wy);
        break;
    }

    case Tool::TexturePaint: {
        texturePaintAt(wx, wy);
        break;
    }

    case Tool::WaterTool: {
        waterToolAt(wx, wy);
        break;
    }

    case Tool::PlaceEnemy: {
        uint64_t newId = scene_.allocateEntityId();
        auto cmd = std::make_unique<PlaceEnemyCommand>(wx, wy, newId, "Enemy");
        commandStack_.execute(std::move(cmd), scene_, world_);
        setSelection(newId);
        addLog("Placed enemy.");
        break;
    }

    case Tool::Select: {
        uint64_t hit = 0;
        float best = 2.f;
        for (auto& e : scene_.entities) {
            const dash::editor::Transform3D w = dash::editor::worldTransform(scene_, e.id);
            float dx = w.x - wx;
            float dy = w.y - wy;
            float d  = std::sqrt(dx * dx + dy * dy);
            if (d < best) { best = d; hit = e.id; }
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeySuper || io.KeyCtrl) {
            if (hit != 0) toggleSelection(hit);
        } else {
            setSelection(hit);
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
            clearSelection();
        }
        break;
    }
    }
}

void EditorApp::paintTileAt(float wx, float wy)
{
    int cx = (int)wx, cy = (int)wy;
    int r = brushSize_ - 1;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            int tx = cx + dx, ty = cy + dy;
            if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) continue;
            if (world_.grid[ty][tx].type == selectedTileType_) continue;
            auto cmd = std::make_unique<PaintTileCommand>(tx, ty, selectedTileType_);
            commandStack_.execute(std::move(cmd), scene_, world_);
        }
    }
}

void EditorApp::floodFillAt(float wx, float wy)
{
    int tx = (int)wx, ty = (int)wy;
    if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) return;
    if (world_.terrain().face(tx, ty).type == selectedTileType_) return;

    auto cmd = std::make_unique<FloodFillCommand>(tx, ty, selectedTileType_);
    commandStack_.execute(std::move(cmd), scene_, world_);
    addLog("Flood fill applied.");
}

void EditorApp::heightBrushAt(float wx, float wy)
{
    // Find nearest vertex
    int vx = static_cast<int>(std::round(wx));
    int vy = static_cast<int>(std::round(wy));
    if (vx < 0 || vx >= TerrainMesh::VW || vy < 0 || vy >= TerrainMesh::VH) return;

    HeightBrushCommand::Mode mode;
    switch (heightBrushMode_) {
    case HeightBrushMode::Raise:   mode = HeightBrushCommand::Mode::Raise;   break;
    case HeightBrushMode::Lower:   mode = HeightBrushCommand::Mode::Lower;   break;
    case HeightBrushMode::Smooth:  mode = HeightBrushCommand::Mode::Smooth;  break;
    case HeightBrushMode::Flatten: mode = HeightBrushCommand::Mode::Flatten; break;
    }

    auto cmd = std::make_unique<HeightBrushCommand>(
        vx, vy, heightBrushRadius_, heightBrushStrength_, mode);
    commandStack_.execute(std::move(cmd), scene_, world_);
}

void EditorApp::cliffBrushAt(float wx, float wy)
{
    int vx = static_cast<int>(std::round(wx));
    int vy = static_cast<int>(std::round(wy));
    if (vx < 0 || vx >= TerrainMesh::VW || vy < 0 || vy >= TerrainMesh::VH) return;

    auto cmd = std::make_unique<CliffBrushCommand>(
        vx, vy, cliffBrushRadius_, cliffBrushMode_);
    commandStack_.execute(std::move(cmd), scene_, world_);
}

void EditorApp::texturePaintAt(float wx, float wy)
{
    int vx = static_cast<int>(std::round(wx));
    int vy = static_cast<int>(std::round(wy));
    if (vx < 0 || vx >= TerrainMesh::VW || vy < 0 || vy >= TerrainMesh::VH) return;

    auto cmd = std::make_unique<TexturePaintCommand>(
        vx, vy, texturePaintRadius_, texturePaintStrength_, selectedTexture_);
    commandStack_.execute(std::move(cmd), scene_, world_);
}

// Water level is per-body, not per-position; wx/wy only keep the call site uniform with the other tools.
void EditorApp::waterToolAt(float, float)
{
    // Water tool sets/adjusts water level for the selected water body
    TerrainMesh& tm = world_.terrain();
    bool found = false;
    for (auto& wb : tm.waterBodies()) {
        if (wb.id == static_cast<uint8_t>(selectedWaterBodyId_)) {
            float oldLevel = wb.waterLevel;
            auto cmd = std::make_unique<WaterLevelCommand>(
                wb.id, oldLevel, waterLevel_);
            commandStack_.execute(std::move(cmd), scene_, world_);
            found = true;
            break;
        }
    }
    if (!found) {
        // Create a new water body
        WaterBody wb;
        wb.id = static_cast<uint8_t>(selectedWaterBodyId_);
        wb.waterLevel = waterLevel_;
        wb.opacity = 0.6f;
        wb.tint = {0.08f, 0.14f, 0.31f};
        tm.addWaterBody(wb);
        tm.markDirty();
        scene_.modified = true;
    }
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

float EditorApp::tileHeight(TileType type) const
{
    switch (type) {
        case TileType::DeepWater: return -0.30f;
        case TileType::Water:     return -0.16f;
        case TileType::Sand:      return 0.00f;
        case TileType::Grass:     return 0.05f;
        case TileType::Forest:    return 0.14f;
        case TileType::Dirt:      return 0.08f;
        case TileType::Stone:     return 0.22f;
        case TileType::Mountain:  return 0.42f;
        case TileType::Snow:      return 0.50f;
    }
    return 0.0f;
}

float EditorApp::entityWorldZ(uint64_t entityId) const
{
    for (const auto& e : scene_.entities) {
        if (e.id != entityId) continue;
        for (const auto& c : e.components) {
            if (const auto* tf = std::get_if<TransformComponent>(&c)) return tf->z;
        }
        return 0.0f;
    }
    return 0.0f;
}

bool EditorApp::viewportScreenToWorld(float vx, float vy, float& wx, float& wy)
{
    // Build the same viewProj used for rendering
    float viewProj[16];
    buildViewProjMatrix(vpDisplayW_, vpDisplayH_, viewProj);

    // Invert the viewProj matrix (4x4 cofactor inverse)
    float inv[16];
    {
        const float* m = viewProj;
        float a00 = m[0], a01 = m[1], a02 = m[2],  a03 = m[3];
        float a10 = m[4], a11 = m[5], a12 = m[6],  a13 = m[7];
        float a20 = m[8], a21 = m[9], a22 = m[10], a23 = m[11];
        float a30 = m[12],a31 = m[13],a32 = m[14], a33 = m[15];

        float b00 = a00*a11 - a01*a10, b01 = a00*a12 - a02*a10;
        float b02 = a00*a13 - a03*a10, b03 = a01*a12 - a02*a11;
        float b04 = a01*a13 - a03*a11, b05 = a02*a13 - a03*a12;
        float b06 = a20*a31 - a21*a30, b07 = a20*a32 - a22*a30;
        float b08 = a20*a33 - a23*a30, b09 = a21*a32 - a22*a31;
        float b10 = a21*a33 - a23*a31, b11 = a22*a33 - a23*a32;

        float det = b00*b11 - b01*b10 + b02*b09 + b03*b08 - b04*b07 + b05*b06;
        if (std::abs(det) < 1e-12f) return false;
        float invDet = 1.0f / det;

        inv[0]  = ( a11*b11 - a12*b10 + a13*b09) * invDet;
        inv[1]  = (-a01*b11 + a02*b10 - a03*b09) * invDet;
        inv[2]  = ( a31*b05 - a32*b04 + a33*b03) * invDet;
        inv[3]  = (-a21*b05 + a22*b04 - a23*b03) * invDet;
        inv[4]  = (-a10*b11 + a12*b08 - a13*b07) * invDet;
        inv[5]  = ( a00*b11 - a02*b08 + a03*b07) * invDet;
        inv[6]  = (-a30*b05 + a32*b02 - a33*b01) * invDet;
        inv[7]  = ( a20*b05 - a22*b02 + a23*b01) * invDet;
        inv[8]  = ( a10*b10 - a11*b08 + a13*b06) * invDet;
        inv[9]  = (-a00*b10 + a01*b08 - a03*b06) * invDet;
        inv[10] = ( a30*b04 - a31*b02 + a33*b00) * invDet;
        inv[11] = (-a20*b04 + a21*b02 - a23*b00) * invDet;
        inv[12] = (-a10*b09 + a11*b07 - a12*b06) * invDet;
        inv[13] = ( a00*b09 - a01*b07 + a02*b06) * invDet;
        inv[14] = (-a30*b03 + a31*b01 - a32*b00) * invDet;
        inv[15] = ( a20*b03 - a21*b01 + a22*b00) * invDet;
    }

    // Convert viewport mouse coords to Vulkan NDC (Y: -1=top, +1=bottom)
    float ndcX = (2.0f * vx / vpDisplayW_) - 1.0f;
    float ndcY = (2.0f * vy / vpDisplayH_) - 1.0f;

    // Unproject near and far points through inverse viewProj
    auto unproject = [&](float ndcZ, float& outX, float& outY, float& outZ) {
        float x = inv[0]*ndcX + inv[4]*ndcY + inv[8]*ndcZ  + inv[12];
        float y = inv[1]*ndcX + inv[5]*ndcY + inv[9]*ndcZ  + inv[13];
        float z = inv[2]*ndcX + inv[6]*ndcY + inv[10]*ndcZ + inv[14];
        float w = inv[3]*ndcX + inv[7]*ndcY + inv[11]*ndcZ + inv[15];
        if (std::abs(w) < 1e-12f) return false;
        outX = x / w; outY = y / w; outZ = z / w;
        return true;
    };

    float nearX, nearY, nearZ, farX, farY, farZ;
    if (!unproject(0.0f, nearX, nearY, nearZ)) return false;  // Vulkan near = 0
    if (!unproject(1.0f, farX,  farY,  farZ))  return false;  // Vulkan far = 1

    // Ray direction
    float dirX = farX - nearX, dirY = farY - nearY, dirZ = farZ - nearZ;

    // Intersect ray with terrain plane y = 0
    if (std::abs(dirY) < 1e-6f) return false;  // ray parallel to ground
    float t = -nearY / dirY;
    if (t < 0.0f) return false;  // intersection behind camera

    float hitX = nearX + t * dirX;
    float hitZ = nearZ + t * dirZ;

    // Convert from 3D world space to tile coords
    wx = hitX / TILE_SCALE;
    wy = hitZ / TILE_SCALE;
    return (wx >= 0 && wx < WORLD_W && wy >= 0 && wy < WORLD_H);
}
