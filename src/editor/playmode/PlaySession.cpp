#include "PlaySession.h"

// ─────────────────────────────────────────────────────────────────────────────
void PlaySession::capture(const SceneData& scene, const World& world)
{
    sceneSnapshot_ = scene;
    worldSnapshot_.grid = world.grid;  // std::vector copy
    captured_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
void PlaySession::restore(SceneData& scene, World& world) const
{
    if (!captured_) return;

    scene = sceneSnapshot_;
    world.grid = worldSnapshot_.grid;  // std::vector copy
}
