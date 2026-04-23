#include "HeightBrushCommand.h"
#include <cmath>
#include <algorithm>

HeightBrushCommand::HeightBrushCommand(int centerVX, int centerVY,
                                       int radius, float strength, Mode mode)
    : centerVX_(centerVX), centerVY_(centerVY),
      radius_(radius), strength_(strength), mode_(mode)
{}

void HeightBrushCommand::apply(SceneData& scene, World& world)
{
    TerrainMesh& tm = world.terrain();
    constexpr int VW = TerrainMesh::VW;
    constexpr int VH = TerrainMesh::VH;

    if (!captured_) {
        // Collect affected vertices within radius
        for (int dy = -radius_; dy <= radius_; ++dy) {
            for (int dx = -radius_; dx <= radius_; ++dx) {
                int vx = centerVX_ + dx;
                int vy = centerVY_ + dy;
                if (vx < 0 || vx >= VW || vy < 0 || vy >= VH) continue;

                float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (dist > radius_) continue;

                float oldH = tm.vert(vx, vy).height;
                float falloff = 1.0f - (dist / (radius_ + 1.0f));
                float delta = strength_ * falloff;

                float newH = oldH;
                switch (mode_) {
                case Mode::Raise:
                    newH = oldH + delta;
                    break;
                case Mode::Lower:
                    newH = oldH - delta;
                    break;
                case Mode::Smooth: {
                    // Average with neighbors
                    float sum = 0.f;
                    int count = 0;
                    for (int ny = std::max(0, vy-1); ny <= std::min(VH-1, vy+1); ++ny) {
                        for (int nx = std::max(0, vx-1); nx <= std::min(VW-1, vx+1); ++nx) {
                            sum += tm.vert(nx, ny).height;
                            ++count;
                        }
                    }
                    float avg = sum / count;
                    newH = oldH + (avg - oldH) * delta;
                    break;
                }
                case Mode::Flatten: {
                    float targetH = tm.vert(centerVX_, centerVY_).height;
                    newH = oldH + (targetH - oldH) * falloff * 0.5f;
                    break;
                }
                }

                newH = std::max(0.0f, std::min(1.0f, newH));
                affected_.push_back({vx, vy, oldH, newH});
            }
        }
        captured_ = true;
    }

    for (auto& r : affected_)
        tm.vert(r.vx, r.vy).height = r.newHeight;

    tm.markDirty();
    scene.modified = true;
}

void HeightBrushCommand::undo(SceneData& scene, World& world)
{
    TerrainMesh& tm = world.terrain();
    for (auto& r : affected_)
        tm.vert(r.vx, r.vy).height = r.oldHeight;

    tm.markDirty();
    scene.modified = true;
}
