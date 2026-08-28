#include "rendering/vulkan/EditorBridge.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

namespace dash::vkexp {

// A single step advances one nominal 60 Hz frame regardless of how long the
// paused editor took to send it, so stepping is deterministic.
float EditorBridge::applyPlaybackScale(float dt)
{
    if (!playback_.paused) return dt * playback_.timeScale;
    if (stepPending_) {
        stepPending_ = false;
        return (1.0f / 60.0f) * playback_.timeScale;
    }
    return 0.0f;
}

using json = nlohmann::json;

void EditorBridge::poll(GLFWwindow* window, CameraController& camera,
                        dash::physics::Transform3& cubeTransform)
{
    if (statePath_.empty()) return;

    static bool s_loggedStatePath = false;
    static bool s_loggedOpenFail = false;
    static bool s_loggedParseFail = false;
    static int s_stateReadCounter = 0;
    if (!s_loggedStatePath) {
        std::fprintf(stderr, "[VSTEP] editor state sync enabled path=%s\n", statePath_.c_str());
        s_loggedStatePath = true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (lastRead_.time_since_epoch().count() != 0) {
        const auto deltaMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRead_).count();
        if (deltaMs < 16) return;
    }
    lastRead_ = now;

    std::ifstream in(statePath_);
    if (!in.is_open()) {
        if (!s_loggedOpenFail) {
            std::fprintf(stderr, "[VFAIL] could not open editor state file: %s\n", statePath_.c_str());
            s_loggedOpenFail = true;
        }
        return;
    }
    s_loggedOpenFail = false;

    json j;
    try {
        in >> j;
    } catch (...) {
        if (!s_loggedParseFail) {
            std::fprintf(stderr, "[VFAIL] invalid JSON in editor state file: %s\n", statePath_.c_str());
            s_loggedParseFail = true;
        }
        return;
    }
    s_loggedParseFail = false;
    ++s_stateReadCounter;

    // ── Camera sync ──────────────────────────────────────────────────────
    if (j.contains("camera") && j["camera"].is_object()) {
        const auto& c = j["camera"];
        const float targetX = c.value("x", camera.x());
        const float targetZ = c.value("z", c.value("forward", camera.z()));
        const float zoom = std::max(0.10f, c.value("zoom", 1.0f));
        const float editorYaw = c.value("isoYawDeg", camera.yawDegrees());
        const float editorPitch = c.value("isoPitchDeg", std::fabs(camera.pitchDegrees()));
        const float followDistance = std::max(0.10f, c.value("followDistance", 8.0f));
        const float followHeight = std::max(0.0f, c.value("followHeight", 2.5f));

        camera.applyEditorCamera(targetX, targetZ, zoom, editorYaw, editorPitch,
                                  followDistance, followHeight);
    }

    // ── Selection sync ───────────────────────────────────────────────────
    hasExternalSelection_ = false;
    if (j.contains("selection") && j["selection"].is_object()) {
        const auto& s = j["selection"];
        const uint64_t entityId = s.value("entityId", static_cast<uint64_t>(0));
        if (entityId != 0) {
            const float sx = s.value("x", cubeTransform.position.x);
            const float sy = s.value("y", cubeTransform.position.z);
            const float sz = s.value("z", cubeTransform.position.y);
            cubeTransform.position.x = sx;
            cubeTransform.position.y = sz;
            cubeTransform.position.z = sy;
            hasExternalSelection_ = true;
        }
    }

    // ── Playback transport ───────────────────────────────────────────────
    if (j.contains("playback") && j["playback"].is_object()) {
        const auto& p = j["playback"];
        playback_.paused = p.value("paused", false);
        playback_.timeScale = std::max(0.0f, p.value("timeScale", 1.0f));
        const uint32_t serial = p.value("stepSerial", static_cast<uint32_t>(0));
        if (serial != lastStepSerial_) {
            lastStepSerial_ = serial;
            stepPending_ = true;
        }
        playback_.stepSerial = serial;
    }

    // ── Viewport / lighting / fog / window docking ───────────────────────
    if (embeddedPreview_ && window && j.contains("viewport") && j["viewport"].is_object()) {
        const auto& vp = j["viewport"];

        bool fogEnabled = vp.value("fogEnabled", true);
        fog_.start = fogEnabled ? vp.value("fogStart", 150.0f) : 9999.0f;
        fog_.end   = fogEnabled ? vp.value("fogEnd", 400.0f) : 9999.0f;

        lighting_.dirX      = vp.value("lightDirX", 0.3f);
        lighting_.dirY      = vp.value("lightDirY", 0.9f);
        lighting_.dirZ      = vp.value("lightDirZ", 0.2f);
        lighting_.colorR    = vp.value("lightColorR", 1.0f);
        lighting_.colorG    = vp.value("lightColorG", 0.98f);
        lighting_.colorB    = vp.value("lightColorB", 0.92f);
        lighting_.intensity = vp.value("lightIntensity", 1.3f);
        lighting_.ambient   = vp.value("ambientStrength", 0.55f);
        lighting_.specStr   = vp.value("specularStrength", 0.15f);
        lighting_.specShin  = vp.value("specularShininess", 32.0f);

        int sx = static_cast<int>(vp.value("screenX", 0.0f));
        int sy = static_cast<int>(vp.value("screenY", 0.0f));
        int sw = std::max(64, static_cast<int>(vp.value("screenW", 640.0f)));
        int sh = std::max(64, static_cast<int>(vp.value("screenH", 360.0f)));

        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_TRUE);
        glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_FALSE);
        glfwSetWindowPos(window, sx, sy);
        glfwSetWindowSize(window, sw, sh);

        if (!loggedEmbeddedDocking_) {
            int wx = 0, wy = 0, ww = 0, wh = 0;
            glfwGetWindowPos(window, &wx, &wy);
            glfwGetWindowSize(window, &ww, &wh);
            std::fprintf(stderr,
                         "[D84] Embedded docking applied: target=(%d,%d %dx%d) actual=(%d,%d %dx%d)\n",
                         sx, sy, sw, sh, wx, wy, ww, wh);
            loggedEmbeddedDocking_ = true;
        }

        if ((s_stateReadCounter % 120) == 1) {
            int wx = 0, wy = 0, ww = 0, wh = 0;
            glfwGetWindowPos(window, &wx, &wy);
            glfwGetWindowSize(window, &ww, &wh);
            std::fprintf(stderr,
                         "[VSTEP] state tick #%d cam=(%.2f,%.2f,%.2f) yaw=%.2f pitch=%.2f dock target=(%d,%d %dx%d) actual=(%d,%d %dx%d)\n",
                         s_stateReadCounter,
                         camera.x(), camera.y(), camera.z(),
                         camera.yawDegrees(), camera.pitchDegrees(),
                         sx, sy, sw, sh,
                         wx, wy, ww, wh);
        }
    }
}

} // namespace dash::vkexp
