#include "PlaySession.h"
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
void PlaySession::capture(const SceneData& scene, const World& world)
{
    sceneSnapshot_ = scene;
    // Deep-copy tile grid (POD array – memcpy is fine)
    std::memcpy(worldSnapshot_.grid, world.grid, sizeof(world.grid));
    captured_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
void PlaySession::restore(SceneData& scene, World& world) const
{
    if (!captured_) return;

    scene = sceneSnapshot_;
    std::memcpy(world.grid, worldSnapshot_.grid, sizeof(world.grid));
}
