#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// AnimationPanel — clip playback and preview for the animated entities
//
// The preview is the viewport itself: the panel writes the entity's
// AnimationComponent (which EditorApp::updateViewportAnimators re-reads every
// frame) and, for scrubbing, drives the live AnimationPlayer directly.
// ─────────────────────────────────────────────────────────────────────────────

#include "rendering/animation/AnimationPlayer.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace dash::anim { class AnimationSetCache; }
struct SceneData;

namespace dash::editor::animpanel {

/// AnimationPlayer has no seek, and the renderer is off limits: restarting the
/// clip and replaying `seconds` of it at speed 1 lands on the requested pose
/// without adding an API to src/rendering. Playback flags are restored so a
/// scrub does not silently resume a paused preview.
inline bool scrubTo(dash::anim::AnimationPlayer& player, const std::string& clip, float seconds)
{
    const bool  wasPaused = player.paused();
    const float wasSpeed  = player.speed();

    player.stop();
    const bool ok = player.play(clip, 0.0f);
    if (ok) {
        player.setPaused(false);
        player.setSpeed(1.0f);
        if (seconds > 0.0f) player.update(seconds);
    }
    player.setSpeed(wasSpeed);
    player.setPaused(wasPaused);
    return ok;
}

} // namespace dash::editor::animpanel

class AnimationPanel {
public:
    using LogCallback      = std::function<void(const std::string&)>;
    using PlayerMap        = std::unordered_map<uint64_t, dash::anim::AnimationPlayer>;
    using MeshPathResolver = std::function<std::string(const std::string&)>;

    void draw(SceneData& scene,
              uint64_t selectedEntityId,
              dash::anim::AnimationSetCache& sets,
              PlayerMap& players,
              const MeshPathResolver& resolveMeshPath,
              LogCallback logCb = nullptr);

    uint64_t targetEntity() const { return target_; }

private:
    uint64_t    target_ = 0;
    bool        followSelection_ = true;
    std::string status_;
};
