#pragma once

#include "rendering/vulkan/VkMath.h"
#include "game/physics/PhysicsWorld.h"
#include "input/InputBindings3D.h"

struct GLFWwindow;

namespace dash::vkexp {

class CameraController {
public:
    // Position the camera from a scene spawn point
    void focusOnSpawn(const dash::physics::Vec3& spawn);

    // Deferred re-focus: compute yaw/pitch to look at a world position
    void requestAutoFocus(float tx, float ty, float tz);
    void applyAutoFocusIfPending();

    // Right-click mouse look (legacy free-camera mode)
    void updateMouseLook(GLFWwindow* window, const InputBindings3D& bindings);

    // Fixed isometric orbit around the player
    void followPlayer(float px, float py, float pz);

    // Sync camera from editor state JSON values.
    // Returns true if the camera was actually updated.
    bool applyEditorCamera(float targetX, float targetZ, float zoom,
                           float editorYaw, float editorPitch,
                           float followDist, float followHt);

    // Build view-projection matrix for the current camera state
    Mat4 computeViewProjection(float aspectRatio) const;

    // Transient displacement for combat feedback. It moves the eye and its
    // target together, so the shake is a pure translation and the framing does
    // not swing; it is deliberately kept out of x()/y()/z() so gameplay,
    // culling volumes and the shadow cascades never see it.
    void setShakeOffset(float dx, float dy, float dz)
    {
        shakeX_ = dx;
        shakeY_ = dy;
        shakeZ_ = dz;
    }

    // Camera basis vectors, matching the view matrix built above.
    Vec3 forwardVector() const;
    Vec3 rightVector() const;
    Vec3 upVector() const;

    // Accessors
    float x() const { return x_; }
    float y() const { return y_; }
    float z() const { return z_; }
    float yawDegrees() const { return yawDegrees_; }
    float pitchDegrees() const { return pitchDegrees_; }

private:
    float x_ = 0.0f;
    float y_ = 1.5f;
    float z_ = 2.2f;
    float yawDegrees_ = -90.0f;
    float pitchDegrees_ = -20.0f;

    bool pendingAutoFocus_ = false;
    float autoFocusX_ = 0.0f;
    float autoFocusY_ = 0.0f;
    float autoFocusZ_ = 0.0f;

    bool hadLookFrame_ = false;
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;

    float followDistance_ = 8.0f;
    float followHeight_ = 2.5f;

    float shakeX_ = 0.0f;
    float shakeY_ = 0.0f;
    float shakeZ_ = 0.0f;

    // Track editor state to detect changes
    float lastEditorTargetX_ = 0.0f;
    float lastEditorTargetZ_ = 0.0f;
    float lastEditorZoom_ = 1.0f;
    float lastEditorYaw_ = -90.0f;
    float lastEditorPitch_ = 0.0f;
    float lastEditorFollowDistance_ = 8.0f;
    float lastEditorFollowHeight_ = 2.5f;
};

} // namespace dash::vkexp
