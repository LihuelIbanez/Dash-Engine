#pragma once

#include <chrono>
#include <string>

#include "rendering/vulkan/CameraController.h"
#include "game/physics/PhysicsWorld.h"
#include "game/physics/TransformProxy.h"

struct GLFWwindow;

namespace dash::vkexp {

struct LightingParams {
    float dirX = 0.3f, dirY = 0.9f, dirZ = 0.2f;
    float intensity = 1.3f;
    float colorR = 1.0f, colorG = 0.98f, colorB = 0.92f;
    float ambient = 0.55f;
    float specStr = 0.15f;
    float specShin = 32.0f;
};

struct FogParams {
    float start = 150.0f;
    float end = 400.0f;
};

// Play-mode transport shared through the editor state file. `stepSerial` is a
// monotonic counter: the runtime advances exactly one frame whenever it sees a
// value it has not consumed yet, so a step is never lost or replayed.
struct PlaybackControl {
    bool     paused = false;
    float    timeScale = 1.0f;
    uint32_t stepSerial = 0;
};

class EditorBridge {
public:
    void setStatePath(const std::string& path) { statePath_ = path; }
    void setEmbeddedPreview(bool enabled) { embeddedPreview_ = enabled; }

    // Poll the editor state file. Updates camera, selection, lighting,
    // fog, and window docking as needed.
    void poll(GLFWwindow* window, CameraController& camera,
              dash::physics::Transform3& cubeTransform);

    bool hasExternalSelection() const { return hasExternalSelection_; }
    bool isEmbeddedPreview() const { return embeddedPreview_; }
    const std::string& statePath() const { return statePath_; }
    const LightingParams& lighting() const { return lighting_; }
    const FogParams& fog() const { return fog_; }
    const PlaybackControl& playback() const { return playback_; }

    // Scales dt for this frame and consumes a pending single-step request.
    // Returns 0 while paused, so systems advance exactly when asked to.
    float applyPlaybackScale(float dt);

private:
    std::string statePath_;
    bool embeddedPreview_ = false;
    bool hasExternalSelection_ = false;
    bool loggedEmbeddedDocking_ = false;
    std::chrono::steady_clock::time_point lastRead_{};
    LightingParams lighting_;
    FogParams fog_;
    PlaybackControl playback_;
    uint32_t lastStepSerial_ = 0;
    bool stepPending_ = false;
};

} // namespace dash::vkexp
