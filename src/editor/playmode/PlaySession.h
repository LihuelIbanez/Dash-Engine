#pragma once
#include "SceneData.h"
#include "World.h"

// ─────────────────────────────────────────────────────────────────────────────
// PlaySession – captures a snapshot of the editor state before play-testing
//               and restores it when the session ends.
// ─────────────────────────────────────────────────────────────────────────────
class PlaySession {
public:
    // Take a snapshot of the current scene and world state.
    void capture(const SceneData& scene, const World& world);

    // Restore the scene and world to the captured snapshot.
    void restore(SceneData& scene, World& world) const;

    // Whether a snapshot has been captured.
    bool hasSnapshot() const { return captured_; }

private:
    bool      captured_ = false;
    SceneData sceneSnapshot_;
    World     worldSnapshot_;
};
