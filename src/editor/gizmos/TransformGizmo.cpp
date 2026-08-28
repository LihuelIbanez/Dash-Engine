#include "TransformGizmo.h"

#include "imgui.h"

#include <cmath>
#include <cstdio>

namespace dash::gizmo {

namespace {

constexpr float kHandlePixels = 90.f;   // on-screen length of an axis handle
constexpr float kPickFraction = 0.14f;  // pick radius, relative to handle length
constexpr int   kRingSegments = 48;

ImU32 axisColor(Axis axis, bool highlight)
{
    if (highlight) return IM_COL32(255, 214, 64, 255);
    switch (axis) {
    case Axis::X: return IM_COL32(226, 74, 74, 255);
    case Axis::Y: return IM_COL32(112, 209, 96, 255);
    case Axis::Z: return IM_COL32(72, 140, 236, 255);
    case Axis::None: break;
    }
    return IM_COL32(200, 200, 200, 255);
}

const char* axisLabel(Axis axis)
{
    switch (axis) {
    case Axis::X: return "X";
    case Axis::Y: return "Y";
    case Axis::Z: return "Z";
    case Axis::None: break;
    }
    return "-";
}

bool worldToScreen(const float viewProj[16], const ViewportRect& rect,
                   const Vec3& p, ImVec2& out)
{
    float nx = 0.f, ny = 0.f, nz = 0.f;
    if (!projectToNdc(viewProj, p, nx, ny, nz)) return false;
    out.x = rect.x + (nx * 0.5f + 0.5f) * rect.w;
    out.y = rect.y + (ny * 0.5f + 0.5f) * rect.h;
    return true;
}

} // namespace

void TransformGizmo::cancel()
{
    dragging_ = false;
    activeAxis_ = Axis::None;
    rawRotationDeg_ = 0.f;
}

bool TransformGizmo::buildRay(const GizmoInput& in, Ray& out) const
{
    if (!in.viewProj || in.rect.w < 1.f || in.rect.h < 1.f) return false;
    float inv[16];
    if (!invertMatrix4(in.viewProj, inv)) return false;
    const float ndcX = 2.f * (in.mouseX - in.rect.x) / in.rect.w - 1.f;
    const float ndcY = 2.f * (in.mouseY - in.rect.y) / in.rect.h - 1.f;
    return rayFromNdc(inv, ndcX, ndcY, out);
}

float TransformGizmo::handleLength(const GizmoInput& in) const
{
    if (!in.viewProj) return 1.f;

    ImVec2 center;
    if (!worldToScreen(in.viewProj, in.rect, in.pivot, center)) return 1.f;

    float pixelsPerUnit = 0.f;
    int samples = 0;
    for (int i = 1; i <= 3; ++i) {
        ImVec2 tip;
        const Vec3 p = add(in.pivot, axisDirection(static_cast<Axis>(i)));
        if (!worldToScreen(in.viewProj, in.rect, p, tip)) continue;
        const float dx = tip.x - center.x;
        const float dy = tip.y - center.y;
        pixelsPerUnit += std::sqrt(dx * dx + dy * dy);
        ++samples;
    }
    if (samples == 0 || pixelsPerUnit < 1e-3f) return 1.f;
    pixelsPerUnit /= static_cast<float>(samples);
    return kHandlePixels / pixelsPerUnit;
}

GizmoOutput TransformGizmo::update(const GizmoInput& in)
{
    GizmoOutput out;
    if (mode_ == Mode::None) { cancel(); return out; }

    Ray ray;
    const bool haveRay = buildRay(in, ray);
    const float len = dragging_ ? dragLength_ : handleLength(in);
    const float pick = len * kPickFraction;

    if (!dragging_) {
        hoverAxis_ = Axis::None;
        if (haveRay && in.hovered) {
            hoverAxis_ = (mode_ == Mode::Rotate)
                       ? pickRotationRing(ray, in.pivot, len, pick)
                       : pickAxisHandle(ray, in.pivot, len, pick);
        }
        out.handleHovered = (hoverAxis_ != Axis::None);

        if (out.handleHovered && in.mouseClicked) {
            dragging_ = true;
            activeAxis_ = hoverAxis_;
            dragStartRay_ = ray;
            prevRay_ = ray;
            dragPivot_ = in.pivot;
            dragLength_ = len;
            rawRotationDeg_ = 0.f;
            out.dragStarted = true;
        } else {
            return out;
        }
    }

    out.dragging = true;
    out.handleHovered = true;
    out.axis = activeAxis_;

    const Vec3 dir = axisDirection(activeAxis_);
    if (haveRay) {
        switch (mode_) {
        case Mode::Translate: {
            float delta = 0.f;
            if (axisDragDelta(dragStartRay_, ray, dragPivot_, dir, delta)) {
                if (in.snap) delta = snapTo(delta, translateSnap_);
                out.translation = mul(dir, delta);
            }
            break;
        }
        case Mode::Rotate: {
            float frameDelta = 0.f;
            if (axisRotationDelta(prevRay_, ray, dragPivot_, dir, frameDelta))
                rawRotationDeg_ += frameDelta;
            out.rotationDeg = in.snap ? snapAngleDeg(rawRotationDeg_, rotateSnapDeg_)
                                      : rawRotationDeg_;
            break;
        }
        case Mode::Scale: {
            float delta = 0.f;
            if (axisDragDelta(dragStartRay_, ray, dragPivot_, dir, delta)) {
                float factor = 1.f + delta / (dragLength_ > 1e-4f ? dragLength_ : 1.f);
                if (factor < 0.01f) factor = 0.01f;
                if (in.snap) {
                    factor = snapTo(factor, scaleSnap_);
                    if (factor < scaleSnap_) factor = scaleSnap_;
                }
                out.scaleFactor = factor;
            }
            break;
        }
        case Mode::None:
            break;
        }
        prevRay_ = ray;
    }

    if (in.mouseReleased || !in.mouseDown) {
        dragging_ = false;
        out.dragging = false;
        out.dragEnded = true;
        activeAxis_ = Axis::None;
        rawRotationDeg_ = 0.f;
    }
    return out;
}

void TransformGizmo::draw(ImDrawList* dl, const GizmoInput& in, const GizmoOutput& out) const
{
    if (!dl || !in.viewProj || mode_ == Mode::None) return;

    ImVec2 center;
    if (!worldToScreen(in.viewProj, in.rect, in.pivot, center)) return;

    const float len = dragging_ ? dragLength_ : handleLength(in);
    const Axis highlight = dragging_ ? activeAxis_ : hoverAxis_;

    dl->PushClipRect({in.rect.x, in.rect.y},
                     {in.rect.x + in.rect.w, in.rect.y + in.rect.h}, true);

    if (mode_ == Mode::Rotate) {
        for (int i = 1; i <= 3; ++i) {
            const Axis axis = static_cast<Axis>(i);
            const Vec3 n = axisDirection(axis);
            const Vec3 u = normalize(cross((std::fabs(n.x) < 0.9f) ? Vec3{1.f, 0.f, 0.f}
                                                                  : Vec3{0.f, 1.f, 0.f}, n));
            const Vec3 v = cross(n, u);

            bool started = false;
            for (int s = 0; s <= kRingSegments; ++s) {
                const float a = 2.f * 3.14159265f * static_cast<float>(s) / kRingSegments;
                const Vec3 p = add(in.pivot, add(mul(u, std::cos(a) * len),
                                                 mul(v, std::sin(a) * len)));
                ImVec2 sp;
                if (!worldToScreen(in.viewProj, in.rect, p, sp)) { started = false; continue; }
                if (!started) { dl->PathClear(); started = true; }
                dl->PathLineTo(sp);
            }
            if (started) dl->PathStroke(axisColor(axis, axis == highlight), 0, 2.2f);
        }
    } else {
        for (int i = 1; i <= 3; ++i) {
            const Axis axis = static_cast<Axis>(i);
            const Vec3 tipWorld = add(in.pivot, mul(axisDirection(axis), len));
            ImVec2 tip;
            if (!worldToScreen(in.viewProj, in.rect, tipWorld, tip)) continue;

            const ImU32 col = axisColor(axis, axis == highlight);
            dl->AddLine(center, tip, col, 2.6f);

            const float dx = tip.x - center.x;
            const float dy = tip.y - center.y;
            const float dLen = std::sqrt(dx * dx + dy * dy);
            if (dLen < 1e-3f) continue;
            const float nx = dx / dLen;
            const float ny = dy / dLen;

            if (mode_ == Mode::Translate) {
                const float head = 11.f;
                const ImVec2 a{tip.x, tip.y};
                const ImVec2 b{tip.x - nx * head - ny * head * 0.45f,
                               tip.y - ny * head + nx * head * 0.45f};
                const ImVec2 c{tip.x - nx * head + ny * head * 0.45f,
                               tip.y - ny * head - nx * head * 0.45f};
                dl->AddTriangleFilled(a, b, c, col);
            } else {
                dl->AddRectFilled({tip.x - 5.f, tip.y - 5.f}, {tip.x + 5.f, tip.y + 5.f}, col);
            }
            dl->AddText({tip.x + nx * 8.f - 4.f, tip.y + ny * 8.f - 7.f}, col, axisLabel(axis));
        }
    }

    dl->AddCircleFilled(center, 4.f, IM_COL32(235, 235, 235, 220));
    dl->AddCircle(center, 4.f, IM_COL32(20, 20, 20, 200), 0, 1.5f);

    if (out.dragging) {
        char buf[96];
        switch (mode_) {
        case Mode::Translate:
            std::snprintf(buf, sizeof(buf), "%s  %+.2f %+.2f %+.2f", axisLabel(out.axis),
                          static_cast<double>(out.translation.x),
                          static_cast<double>(out.translation.y),
                          static_cast<double>(out.translation.z));
            break;
        case Mode::Rotate:
            std::snprintf(buf, sizeof(buf), "%s  %+.1f deg", axisLabel(out.axis),
                          static_cast<double>(out.rotationDeg));
            break;
        case Mode::Scale:
            std::snprintf(buf, sizeof(buf), "%s  x%.2f", axisLabel(out.axis),
                          static_cast<double>(out.scaleFactor));
            break;
        case Mode::None:
            buf[0] = '\0';
            break;
        }
        const ImVec2 pos{center.x + 14.f, center.y - 26.f};
        const ImVec2 size = ImGui::CalcTextSize(buf);
        dl->AddRectFilled({pos.x - 4.f, pos.y - 3.f},
                          {pos.x + size.x + 4.f, pos.y + size.y + 3.f},
                          IM_COL32(20, 20, 20, 210), 3.f);
        dl->AddText(pos, IM_COL32(255, 255, 255, 255), buf);
    }

    dl->PopClipRect();
}

} // namespace dash::gizmo
