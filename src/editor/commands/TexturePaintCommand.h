#pragma once
#include "ICommand.h"
#include "SceneData.h"
#include "World.h"
#include "IsoRenderer.h"
#include <vector>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// TexturePaintCommand – paint terrain texture blend weights (WC3-style)
// ─────────────────────────────────────────────────────────────────────────────
class TexturePaintCommand : public ICommand {
public:
    TexturePaintCommand(int centerVX, int centerVY, int radius,
                        float strength, TerrainTextureId texture);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return "Texture Paint"; }

private:
    int   centerVX_, centerVY_;
    int   radius_;
    float strength_;
    TerrainTextureId texture_;

    struct TexRecord {
        int     vx, vy;
        uint8_t oldIndices[4];
        uint8_t oldWeights[4];
        uint8_t newIndices[4];
        uint8_t newWeights[4];
    };
    std::vector<TexRecord> affected_;
    bool captured_ = false;
};
