#include "rendering/vulkan/CameraController.h"

#include <cmath>
#include <cstdio>

#include <GLFW/glfw3.h>

#include "rendering/IsoRenderer.h" // TILE_SCALE

namespace dash::vkexp {

void CameraController::focusOnSpawn(const dash::physics::Vec3& spawn)
{
    x_ = spawn.x * TILE_SCALE + 3.0f;
    y_ = spawn.y + 2.5f;
    z_ = spawn.z * TILE_SCALE + 3.0f;
    yawDegrees_ = -135.0f;
    pitchDegrees_ = -28.0f;
    // Set auto-focus target to the spawn so applyAutoFocusIfPending()
    // can compute yaw/pitch from the actual entity position.
    autoFocusX_ = spawn.x;
    autoFocusY_ = spawn.y;
    autoFocusZ_ = spawn.z;
    pendingAutoFocus_ = true;
}

void CameraController::requestAutoFocus(float tx, float ty, float tz)
{
    autoFocusX_ = tx;
    autoFocusY_ = ty;
    autoFocusZ_ = tz;
    pendingAutoFocus_ = true;
}

void CameraController::applyAutoFocusIfPending()
{
    if (!pendingAutoFocus_) return;

    const float dx = autoFocusX_ - x_;
    const float dy = autoFocusY_ - y_;
    const float dz = autoFocusZ_ - z_;
    const float flat = std::sqrt(dx * dx + dz * dz);
    if (flat > 0.0001f || std::fabs(dy) > 0.0001f) {
        yawDegrees_ = std::atan2(dz, dx) * 57.2957795f;
        pitchDegrees_ = std::atan2(dy, std::max(0.0001f, flat)) * 57.2957795f;
        if (pitchDegrees_ > 89.0f) pitchDegrees_ = 89.0f;
        if (pitchDegrees_ < -89.0f) pitchDegrees_ = -89.0f;
    }
    pendingAutoFocus_ = false;
}

void CameraController::updateMouseLook(GLFWwindow* window, const InputBindings3D& bindings)
{
    if (glfwGetMouseButton(window, bindings.mouseButtonLook) == GLFW_PRESS) {
        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        if (!hadLookFrame_) {
            hadLookFrame_ = true;
            lastMouseX_ = mouseX;
            lastMouseY_ = mouseY;
        }
        const float dx = static_cast<float>(mouseX - lastMouseX_);
        const float dy = static_cast<float>(mouseY - lastMouseY_);
        lastMouseX_ = mouseX;
        lastMouseY_ = mouseY;

        yawDegrees_ += dx * bindings.mouseSensitivity;
        pitchDegrees_ -= dy * bindings.mouseSensitivity;
        if (pitchDegrees_ > bindings.pitchMax) pitchDegrees_ = bindings.pitchMax;
        if (pitchDegrees_ < bindings.pitchMin) pitchDegrees_ = bindings.pitchMin;
    } else {
        hadLookFrame_ = false;
    }
}

void CameraController::followPlayer(float px, float py, float pz)
{
    const float yawRad = yawDegrees_ * 0.0174532925f;
    const float pitchRad = pitchDegrees_ * 0.0174532925f;
    const float fx = std::cos(yawRad) * std::cos(pitchRad);
    const float fy = std::sin(pitchRad);
    const float fz = std::sin(yawRad) * std::cos(pitchRad);

    x_ = px * TILE_SCALE - fx * followDistance_;
    y_ = py - fy * followDistance_ + followHeight_;
    z_ = pz * TILE_SCALE - fz * followDistance_;
}

bool CameraController::applyEditorCamera(float targetX, float targetZ, float zoom,
                                          float editorYaw, float editorPitch,
                                          float followDist, float followHt)
{
    const float eps = 0.01f;
    bool posChanged = std::fabs(targetX - lastEditorTargetX_) > eps ||
                      std::fabs(targetZ - lastEditorTargetZ_) > eps;
    bool zoomChanged = std::fabs(zoom - lastEditorZoom_) > eps;
    bool angleChanged = std::fabs(editorYaw - lastEditorYaw_) > eps ||
                        std::fabs(editorPitch - lastEditorPitch_) > eps;
    bool followChanged = std::fabs(followDist - lastEditorFollowDistance_) > eps ||
                         std::fabs(followHt - lastEditorFollowHeight_) > eps;

    if (!(posChanged || zoomChanged || angleChanged || followChanged)) {
        return false;
    }

    yawDegrees_ = -(editorYaw + 90.0f);
    pitchDegrees_ = -std::fabs(editorPitch);
    followDistance_ = followDist;
    followHeight_ = followHt;

    if (pitchDegrees_ > 89.0f) pitchDegrees_ = 89.0f;
    if (pitchDegrees_ < -89.0f) pitchDegrees_ = -89.0f;

    const float yawRad = yawDegrees_ * 0.0174532925f;
    const float pitchRad = pitchDegrees_ * 0.0174532925f;
    const float fx = std::cos(yawRad) * std::cos(pitchRad);
    const float fy = std::sin(pitchRad);
    const float fz = std::sin(yawRad) * std::cos(pitchRad);

    const float distance = 22.0f / zoom;
    x_ = targetX * TILE_SCALE - fx * distance;
    y_ = std::max(1.2f, 6.0f - fy * distance);
    z_ = targetZ * TILE_SCALE - fz * distance;

    static int s_camConvLogCount = 0;
    if (s_camConvLogCount < 3) {
        std::fprintf(stderr,
                     "[VSTEP] camera changed: editor(targetX=%.2f,targetZ=%.2f,zoom=%.2f) -> vulkan(camX=%.2f,camZ=%.2f)\n",
                     targetX, targetZ, zoom, x_, z_);
        ++s_camConvLogCount;
    }

    lastEditorTargetX_ = targetX;
    lastEditorTargetZ_ = targetZ;
    lastEditorZoom_ = zoom;
    lastEditorYaw_ = editorYaw;
    lastEditorPitch_ = editorPitch;
    lastEditorFollowDistance_ = followDist;
    lastEditorFollowHeight_ = followHt;

    return true;
}

Mat4 CameraController::computeViewProjection(float aspectRatio) const
{
    const float yaw = yawDegrees_ * 0.0174532925f;
    const float pitch = pitchDegrees_ * 0.0174532925f;
    Vec3 forward = normalize({
        std::cos(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::sin(yaw) * std::cos(pitch)
    });

    const Vec3 target{x_ + forward.x, y_ + forward.y, z_ + forward.z};
    Mat4 view = lookAt({x_, y_, z_}, target, {0.0f, 1.0f, 0.0f});
    Mat4 proj = perspective(60.0f * 0.0174532925f, aspectRatio, 0.1f, 500.0f);
    proj.m[5] *= -1.0f;

    return multiply(proj, view);
}

} // namespace dash::vkexp
