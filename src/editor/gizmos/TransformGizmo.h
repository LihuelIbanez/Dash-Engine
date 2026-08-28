#pragma once
#include "GizmoMath.h"

struct ImDrawList;

// ─────────────────────────────────────────────────────────────────────────────
// TransformGizmo — translate/rotate/scale handles drawn as a viewport overlay.
//
// The gizmo owns only interaction state; it never touches the scene. Callers
// feed it a pivot plus the frame's mouse state and receive an accumulated,
// already-snapped delta measured from where the drag started.
// ─────────────────────────────────────────────────────────────────────────────
namespace dash::gizmo {

struct ViewportRect {
    float x = 0.f, y = 0.f, w = 1.f, h = 1.f;
};

struct GizmoInput {
    const float* viewProj = nullptr;   // 16 floats, column-major
    ViewportRect rect;                 // image area in screen coordinates
    Vec3  pivot;                       // render-world space
    float mouseX = 0.f, mouseY = 0.f;  // screen coordinates
    bool  hovered = false;             // pointer is inside the viewport
    bool  mouseClicked = false;
    bool  mouseDown = false;
    bool  mouseReleased = false;
    bool  snap = false;
};

struct GizmoOutput {
    bool  handleHovered = false;   // a handle is under the pointer
    bool  dragStarted = false;
    bool  dragging = false;
    bool  dragEnded = false;
    Axis  axis = Axis::None;
    Vec3  translation;             // accumulated since drag start
    float rotationDeg = 0.f;
    float scaleFactor = 1.f;
};

class TransformGizmo {
public:
    void setMode(Mode m) { if (!dragging_) mode_ = m; }
    Mode mode() const { return mode_; }
    bool dragging() const { return dragging_; }
    Axis activeAxis() const { return activeAxis_; }

    void  setTranslateSnap(float v) { translateSnap_ = v; }
    void  setRotateSnapDeg(float v) { rotateSnapDeg_ = v; }
    void  setScaleSnap(float v)     { scaleSnap_ = v; }
    float translateSnap() const     { return translateSnap_; }
    float rotateSnapDeg() const     { return rotateSnapDeg_; }
    float scaleSnap() const         { return scaleSnap_; }

    void cancel();

    GizmoOutput update(const GizmoInput& in);
    void draw(ImDrawList* dl, const GizmoInput& in, const GizmoOutput& out) const;

private:
    // World length that keeps the handles at a roughly constant pixel size.
    float handleLength(const GizmoInput& in) const;
    bool  buildRay(const GizmoInput& in, Ray& out) const;

    Mode  mode_ = Mode::Translate;
    Axis  activeAxis_ = Axis::None;
    Axis  hoverAxis_ = Axis::None;
    bool  dragging_ = false;

    Ray   dragStartRay_{};
    Ray   prevRay_{};
    Vec3  dragPivot_{};
    float dragLength_ = 1.f;
    float rawRotationDeg_ = 0.f;   // accumulated per frame so >180 deg works

    float translateSnap_ = 0.5f;
    float rotateSnapDeg_ = 15.f;
    float scaleSnap_ = 0.1f;
};

} // namespace dash::gizmo
