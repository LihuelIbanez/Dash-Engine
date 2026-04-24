#include "TexturePaintCommand.h"
#include <cmath>
#include <algorithm>
#include <cstring>

TexturePaintCommand::TexturePaintCommand(int centerVX, int centerVY,
                                         int radius, float strength,
                                         TerrainTextureId texture)
    : centerVX_(centerVX), centerVY_(centerVY),
      radius_(radius), strength_(strength), texture_(texture)
{}

void TexturePaintCommand::apply(SceneData& scene, World& world)
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

                float falloff = 1.0f - (dist / (radius_ + 1.0f));
                float weight = strength_ * falloff;

                const auto& v = tm.vert(vx, vy);
                TexRecord rec;
                rec.vx = vx;
                rec.vy = vy;
                std::memcpy(rec.oldIndices, v.texIndices, 4);
                std::memcpy(rec.oldWeights, v.texWeights, 4);

                tm.paintTexture(vx, vy, texture_, weight);

                const auto& v2 = tm.vert(vx, vy);
                std::memcpy(rec.newIndices, v2.texIndices, 4);
                std::memcpy(rec.newWeights, v2.texWeights, 4);

                affected_.push_back(rec);
            }
        }
        captured_ = true;
    } else {
        // Re-apply from stored new values
        for (auto& r : affected_) {
            auto& v = tm.vert(r.vx, r.vy);
            std::memcpy(v.texIndices, r.newIndices, 4);
            std::memcpy(v.texWeights, r.newWeights, 4);
        }
    }

    tm.markDirty();
    scene.modified = true;
}

void TexturePaintCommand::undo(SceneData& scene, World& world)
{
    TerrainMesh& tm = world.terrain();
    for (auto& r : affected_) {
        auto& v = tm.vert(r.vx, r.vy);
        std::memcpy(v.texIndices, r.oldIndices, 4);
        std::memcpy(v.texWeights, r.oldWeights, 4);
    }

    tm.markDirty();
    scene.modified = true;
}
