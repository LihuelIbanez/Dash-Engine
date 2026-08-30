// ═════════════════════════════════════════════════════════════════════════════
// EditorApp — selection, hierarchy and transform gizmo glue.
//
// Split out of EditorApp.cpp to keep that file navigable. The gizmo maths and
// the hierarchy composition live in gizmos/ and EntityHierarchy.h; everything
// here is the wiring between them, the scene and the undo stack.
// ═════════════════════════════════════════════════════════════════════════════
#include "EditorApp.h"
#include "EntityHierarchy.h"
#include "IsoRenderer.h"
#include "commands/EditComponentFieldCommand.h"
#include "commands/MultiEditComponentFieldCommand.h"
#include "commands/ReparentEntityCommand.h"
#include "commands/TransformEntitiesCommand.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

using dash::editor::Transform3D;

namespace {

// Pointer travel (in pixels, squared) before a click turns into a marquee.
constexpr float kRectDragThresholdSq = 16.f;

// projectToNdc stops at NDC; the overlay needs pixels inside the viewport image.
bool projectToScreen(const float viewProj[16], const dash::gizmo::Vec3& p,
                     const dash::gizmo::ViewportRect& rect, float& sx, float& sy)
{
    float nx = 0.f, ny = 0.f, nz = 0.f;
    if (!dash::gizmo::projectToNdc(viewProj, p, nx, ny, nz)) return false;
    sx = rect.x + (nx * 0.5f + 0.5f) * rect.w;
    sy = rect.y + (ny * 0.5f + 0.5f) * rect.h;
    return true;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Selection
// ─────────────────────────────────────────────────────────────────────────────
bool EditorApp::isEntitySelected(uint64_t id) const
{
    return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}

void EditorApp::setSelection(uint64_t id)
{
    selection_.clear();
    if (id != 0) selection_.push_back(id);
    selectedEntityId_ = id;
}

void EditorApp::setSelection(const std::vector<uint64_t>& ids)
{
    selection_.clear();
    for (uint64_t id : ids)
        if (id != 0 && !isEntitySelected(id)) selection_.push_back(id);
    selectedEntityId_ = selection_.empty() ? 0 : selection_.back();
}

void EditorApp::toggleSelection(uint64_t id)
{
    if (id == 0) return;
    auto it = std::find(selection_.begin(), selection_.end(), id);
    if (it != selection_.end()) {
        selection_.erase(it);
    } else {
        selection_.push_back(id);
    }
    selectedEntityId_ = selection_.empty() ? 0 : selection_.back();
}

void EditorApp::clearSelection()
{
    selection_.clear();
    selectedEntityId_ = 0;
}

void EditorApp::pruneSelection()
{
    selection_.erase(
        std::remove_if(selection_.begin(), selection_.end(),
                       [this](uint64_t id) { return findEntityById(id) == nullptr; }),
        selection_.end());
    selectedEntityId_ = selection_.empty() ? 0 : selection_.back();
}

// ─────────────────────────────────────────────────────────────────────────────
// Rectangle selection
// ─────────────────────────────────────────────────────────────────────────────
std::vector<uint64_t> EditorApp::entitiesInScreenRect(const float viewProj[16],
                                                      const dash::gizmo::ViewportRect& rect,
                                                      float x0, float y0, float x1, float y1)
{
    const float minX = std::min(x0, x1), maxX = std::max(x0, x1);
    const float minY = std::min(y0, y1), maxY = std::max(y0, y1);

    std::vector<uint64_t> hits;
    for (const auto& e : scene_.entities) {
        float sx = 0.f, sy = 0.f;
        if (!projectToScreen(viewProj, entityGizmoPivot(e.id), rect, sx, sy)) continue;
        if (sx < minX || sx > maxX || sy < minY || sy > maxY) continue;
        hits.push_back(e.id);
    }
    return hits;
}

void EditorApp::updateRectSelection(const float viewProj[16],
                                    const dash::gizmo::ViewportRect& rect,
                                    float mouseX, float mouseY,
                                    bool viewportHovered, bool gizmoOwnsPointer)
{
    // Every other tool paints or places on drag, so the marquee is Select-only.
    if (editorMode_ != EditorMode::Edit || currentTool_ != Tool::Select ||
        gizmoOwnsPointer || gizmo_.dragging() || draggingEntity_)
    {
        rectSelecting_     = false;
        rectSelectPending_ = false;
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();

    if (!rectSelecting_ && !rectSelectPending_) {
        if (viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            rectSelectPending_ = true;
            rectStartX_ = mouseX;
            rectStartY_ = mouseY;
        }
        return;
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (rectSelecting_) {
            const std::vector<uint64_t> hits =
                entitiesInScreenRect(viewProj, rect, rectStartX_, rectStartY_, mouseX, mouseY);
            if (io.KeyCtrl || io.KeySuper) {
                for (uint64_t id : hits)
                    if (!isEntitySelected(id)) selection_.push_back(id);
                selectedEntityId_ = selection_.empty() ? 0 : selection_.back();
            } else {
                setSelection(hits);
            }
            addLog("[SELECT] Marquee selected " + std::to_string(hits.size()) + " entities.");
        }
        rectSelecting_     = false;
        rectSelectPending_ = false;
        return;
    }

    if (rectSelectPending_) {
        const float dx = mouseX - rectStartX_;
        const float dy = mouseY - rectStartY_;
        if (dx * dx + dy * dy > kRectDragThresholdSq) {
            rectSelectPending_ = false;
            rectSelecting_     = true;
        }
    }

    if (!rectSelecting_) return;

    if (ImDrawList* dl = ImGui::GetWindowDrawList()) {
        const ImVec2 a{std::min(rectStartX_, mouseX), std::min(rectStartY_, mouseY)};
        const ImVec2 b{std::max(rectStartX_, mouseX), std::max(rectStartY_, mouseY)};
        dl->AddRectFilled(a, b, IM_COL32(80, 150, 255, 40));
        dl->AddRect(a, b, IM_COL32(120, 190, 255, 220));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Inspector edits
// ─────────────────────────────────────────────────────────────────────────────
void EditorApp::applyComponentFieldEdit(uint64_t entityId, ComponentType compType,
                                        const PropertyInfo& prop,
                                        const PropertyValue& oldVal,
                                        const PropertyValue& newVal)
{
    std::vector<MultiEditComponentFieldCommand::Target> targets;
    for (uint64_t id : selection_) {
        if (id == entityId) continue;
        EntityData* e = findEntityById(id);
        if (!e) continue;
        for (auto& comp : e->components) {
            if (getVariantType(comp) != compType) continue;
            targets.push_back({id, readFieldValue(fieldPtr(comp, prop), prop.type)});
            break;
        }
    }

    if (targets.empty()) {
        commandStack_.execute(std::make_unique<EditComponentFieldCommand>(
            entityId, compType, prop.offset, prop.type, oldVal, newVal, prop.name),
            scene_, world_);
        return;
    }

    targets.insert(targets.begin(), {entityId, oldVal});
    commandStack_.execute(std::make_unique<MultiEditComponentFieldCommand>(
        std::move(targets), compType, prop.offset, prop.type, newVal, prop.name),
        scene_, world_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Hierarchy
// ─────────────────────────────────────────────────────────────────────────────
void EditorApp::reparentEntity(uint64_t childId, uint64_t newParentId)
{
    if (!dash::editor::canReparent(scene_, childId, newParentId)) {
        addLog("[HIER] Reparent rejected (cycle or missing entity).");
        return;
    }
    commandStack_.execute(std::make_unique<ReparentEntityCommand>(childId, newParentId),
                          scene_, world_);
}

void EditorApp::drawHierarchyNode(uint64_t entityId, int depth)
{
    EntityData* e = findEntityById(entityId);
    if (!e || depth > dash::editor::kMaxHierarchyDepth) return;

    const std::vector<uint64_t> children = dash::editor::childrenOf(scene_, entityId);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_SpanAvailWidth
                             | ImGuiTreeNodeFlags_DefaultOpen;
    if (children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (isEntitySelected(entityId)) flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID(static_cast<int>(entityId));
    const bool open = ImGui::TreeNodeEx(e->name.c_str(), flags);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        if (ImGui::GetIO().KeySuper || ImGui::GetIO().KeyCtrl) toggleSelection(entityId);
        else                                                   setSelection(entityId);
    }

    // Drag & drop re-parenting. The payload is the dragged entity id.
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
        ImGui::SetDragDropPayload("DASH_ENTITY", &entityId, sizeof(entityId));
        ImGui::TextUnformatted(e->name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("DASH_ENTITY")) {
            uint64_t dragged = 0;
            std::memcpy(&dragged, p->Data, sizeof(dragged));
            reparentEntity(dragged, entityId);
        }
        ImGui::EndDragDropTarget();
    }

    if (open) {
        for (uint64_t child : children) drawHierarchyNode(child, depth + 1);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

// ─────────────────────────────────────────────────────────────────────────────
// Gizmo
// ─────────────────────────────────────────────────────────────────────────────
dash::gizmo::Vec3 EditorApp::entityGizmoPivot(uint64_t entityId)
{
    const Transform3D w = dash::editor::worldTransform(scene_, entityId);
    // Scene x/y is the tile plane and z is height; render space is y-up. This
    // has to land on the instance origin the renderer draws around, which is the
    // centre of its AABB: any extra offset shows up as a gizmo floating off the
    // object, and a fixed one is wrong the moment meshes stop being unit cubes.
    return { w.x * TILE_SCALE,
             world_.terrain().sampleHeight(w.x, w.y) + w.z,
             w.y * TILE_SCALE };
}

bool EditorApp::selectionGizmoPivot(dash::gizmo::Vec3& outPivot, Transform3D& outScenePivot)
{
    if (selection_.empty()) return false;

    dash::gizmo::Vec3 sum{0.f, 0.f, 0.f};
    Transform3D sceneSum;
    int count = 0;
    for (uint64_t id : selection_) {
        if (!findEntityById(id)) continue;
        const dash::gizmo::Vec3 p = entityGizmoPivot(id);
        const Transform3D w = dash::editor::worldTransform(scene_, id);
        sum.x += p.x; sum.y += p.y; sum.z += p.z;
        sceneSum.x += w.x; sceneSum.y += w.y; sceneSum.z += w.z;
        ++count;
    }
    if (count == 0) return false;

    const float inv = 1.f / static_cast<float>(count);
    outPivot = { sum.x * inv, sum.y * inv, sum.z * inv };
    outScenePivot = Transform3D{};
    outScenePivot.x = sceneSum.x * inv;
    outScenePivot.y = sceneSum.y * inv;
    outScenePivot.z = sceneSum.z * inv;
    return true;
}

void EditorApp::handleGizmoShortcuts(bool viewportFocused)
{
    if (!viewportFocused || gizmo_.dragging()) return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;

    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) gizmo_.setMode(dash::gizmo::Mode::Translate);
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) gizmo_.setMode(dash::gizmo::Mode::Rotate);
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) gizmo_.setMode(dash::gizmo::Mode::Scale);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !selection_.empty()) clearSelection();
}

bool EditorApp::updateViewportGizmo(const float viewProj[16],
                                    const dash::gizmo::ViewportRect& rect,
                                    float mouseX, float mouseY, bool viewportHovered)
{
    pruneSelection();

    dash::gizmo::Vec3 pivot;
    Transform3D scenePivot;
    if (!selectionGizmoPivot(pivot, scenePivot)) return false;

    gizmo_.setTranslateSnap(gizmoTranslateSnapTiles_);
    gizmo_.setRotateSnapDeg(gizmoRotateSnapDeg_);
    gizmo_.setScaleSnap(gizmoScaleSnap_);

    ImGuiIO& io = ImGui::GetIO();
    dash::gizmo::GizmoInput in;
    in.viewProj = viewProj;
    in.rect = rect;
    in.pivot = pivot;
    in.mouseX = mouseX;
    in.mouseY = mouseY;
    in.hovered = viewportHovered;
    in.mouseClicked = viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    in.mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    in.mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    in.snap = gizmoAlwaysSnap_ || io.KeyShift;

    const dash::gizmo::GizmoOutput out = gizmo_.update(in);

    if (out.dragStarted) {
        gizmoDrag_.clear();
        gizmoDragPivot_ = scenePivot;
        for (uint64_t id : selection_) {
            EntityData* e = findEntityById(id);
            if (!e) continue;
            gizmoDrag_.push_back({id,
                                  dash::editor::localTransform(*e),
                                  dash::editor::worldTransform(scene_, id)});
        }
    }

    if (out.dragging && !gizmoDrag_.empty()) {
        for (const auto& entry : gizmoDrag_) {
            Transform3D w = entry.startWorld;
            switch (gizmo_.mode()) {
                case dash::gizmo::Mode::Translate:
                    // Gizmo deltas are render-space; scene x/y is the tile plane.
                    w.x += out.translation.x / TILE_SCALE;
                    w.y += out.translation.z / TILE_SCALE;
                    w.z += out.translation.y;
                    break;
                case dash::gizmo::Mode::Rotate: {
                    const float rad = out.rotationDeg * 3.14159265358979f / 180.f;
                    const float c = std::cos(rad), s = std::sin(rad);
                    const float dx = entry.startWorld.x - gizmoDragPivot_.x;
                    const float dy = entry.startWorld.y - gizmoDragPivot_.y;
                    w.x = gizmoDragPivot_.x + dx * c - dy * s;
                    w.y = gizmoDragPivot_.y + dx * s + dy * c;
                    w.yawDeg = entry.startWorld.yawDeg + out.rotationDeg;
                    break;
                }
                case dash::gizmo::Mode::Scale: {
                    const float f = out.scaleFactor;
                    w.scale = std::max(0.01f, entry.startWorld.scale * f);
                    w.x = gizmoDragPivot_.x + (entry.startWorld.x - gizmoDragPivot_.x) * f;
                    w.y = gizmoDragPivot_.y + (entry.startWorld.y - gizmoDragPivot_.y) * f;
                    break;
                }
            }
            dash::editor::setWorldTransform(scene_, entry.entityId, w);
        }
        scene_.modified = true;
    }

    if (out.dragEnded && !gizmoDrag_.empty()) {
        // Rewind to the pre-drag state so the command owns the whole change.
        std::vector<TransformEntitiesCommand::Entry> entries;
        entries.reserve(gizmoDrag_.size());
        for (const auto& entry : gizmoDrag_) {
            EntityData* e = findEntityById(entry.entityId);
            if (!e) continue;
            TransformEntitiesCommand::Entry ce;
            ce.entityId = entry.entityId;
            ce.oldLocal = entry.startLocal;
            ce.newLocal = dash::editor::localTransform(*e);
            dash::editor::setLocalTransform(*e, entry.startLocal);
            entries.push_back(ce);
        }
        gizmoDrag_.clear();

        const char* label = gizmo_.mode() == dash::gizmo::Mode::Translate ? "Move Entities"
                          : gizmo_.mode() == dash::gizmo::Mode::Rotate    ? "Rotate Entities"
                                                                         : "Scale Entities";
        auto cmd = std::make_unique<TransformEntitiesCommand>(std::move(entries), label);
        if (!cmd->empty()) commandStack_.execute(std::move(cmd), scene_, world_);
    }

    return out.dragging || out.handleHovered;
}

void EditorApp::drawSelectionOverlays(ImDrawList* dl, const float viewProj[16],
                                      const dash::gizmo::ViewportRect& rect)
{
    if (!dl) return;

    // Selection markers.
    for (uint64_t id : selection_) {
        if (!findEntityById(id)) continue;
        const dash::gizmo::Vec3 p = entityGizmoPivot(id);
        float sx = 0.f, sy = 0.f;
        if (!projectToScreen(viewProj, p, rect, sx, sy)) continue;
        const bool active = (id == selectedEntityId_);
        const ImU32 col = active ? IM_COL32(255, 190, 60, 255) : IM_COL32(255, 190, 60, 140);
        dl->AddCircle({sx, sy}, active ? 9.f : 6.f, col, 12, active ? 2.5f : 1.5f);
    }

    // Light entities get a range ring so they are placeable without a shader.
    for (const auto& e : scene_.entities) {
        for (const auto& c : e.components) {
            const auto* lc = std::get_if<LightComponent>(&c);
            if (!lc || !lc->enabled) continue;
            const dash::gizmo::Vec3 p = entityGizmoPivot(e.id);
            float sx = 0.f, sy = 0.f;
            if (!projectToScreen(viewProj, p, rect, sx, sy)) continue;
            const ImU32 col = IM_COL32(static_cast<int>(lc->colorR * 255),
                                       static_cast<int>(lc->colorG * 255),
                                       static_cast<int>(lc->colorB * 255), 200);
            dl->AddCircleFilled({sx, sy}, 4.f, col, 8);

            dash::gizmo::Vec3 edge = p;
            edge.x += lc->range * TILE_SCALE;
            float ex = 0.f, ey = 0.f;
            if (projectToScreen(viewProj, edge, rect, ex, ey))
                dl->AddCircle({sx, sy}, std::fabs(ex - sx), col & 0x60FFFFFF, 24, 1.5f);
        }
    }

    dash::gizmo::Vec3 pivot;
    Transform3D scenePivot;
    if (!selectionGizmoPivot(pivot, scenePivot)) return;

    dash::gizmo::GizmoInput in;
    in.viewProj = viewProj;
    in.rect = rect;
    in.pivot = pivot;
    gizmo_.draw(dl, in, dash::gizmo::GizmoOutput{});
}
