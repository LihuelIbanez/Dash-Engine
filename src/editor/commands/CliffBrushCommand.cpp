#include "CliffBrushCommand.h"
#include <cmath>
#include <algorithm>

CliffBrushCommand::CliffBrushCommand(int centerVX, int centerVY,
                                     int radius, Mode mode)
    : centerVX_(centerVX), centerVY_(centerVY),
      radius_(radius), mode_(mode)
{}

void CliffBrushCommand::apply(SceneData& scene, World& world)
{
    TerrainMesh& tm = world.terrain();
    constexpr int VW = TerrainMesh::VW;
    constexpr int VH = TerrainMesh::VH;

    if (!captured_) {
        for (int dy = -radius_; dy <= radius_; ++dy) {
            for (int dx = -radius_; dx <= radius_; ++dx) {
                int vx = centerVX_ + dx;
                int vy = centerVY_ + dy;
                if (vx < 0 || vx >= VW || vy < 0 || vy >= VH) continue;

                float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (dist > radius_) continue;

                uint8_t oldLevel = tm.cliffLevel(vx, vy);
                uint8_t newLevel = oldLevel;

                switch (mode_) {
                case Mode::Raise:
                    newLevel = static_cast<uint8_t>(std::min(static_cast<int>(oldLevel) + 1, MAX_CLIFF_LEVEL));
                    break;
                case Mode::Lower:
                    newLevel = static_cast<uint8_t>(std::max(static_cast<int>(oldLevel) - 1, 0));
                    break;
                }

                if (newLevel != oldLevel) {
                    affected_.push_back({vx, vy, oldLevel, newLevel});
                }
            }
        }
        captured_ = true;
    }

    for (auto& r : affected_)
        tm.setCliffLevel(r.vx, r.vy, r.newLevel);

    tm.computeSmoothNormals();
    tm.computeAmbientOcclusion();
    tm.markDirty();
    scene.modified = true;
}

void CliffBrushCommand::undo(SceneData& scene, World& world)
{
    TerrainMesh& tm = world.terrain();
    for (auto& r : affected_)
        tm.setCliffLevel(r.vx, r.vy, r.oldLevel);

    tm.computeSmoothNormals();
    tm.computeAmbientOcclusion();
    tm.markDirty();
    scene.modified = true;
}
