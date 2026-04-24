#pragma once
#include "ICommand.h"
#include "SceneData.h"
#include "World.h"
#include <vector>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// CliffBrushCommand – raise/lower discrete cliff levels for WC3-style terrain
// ─────────────────────────────────────────────────────────────────────────────
class CliffBrushCommand : public ICommand {
public:
    enum class Mode { Raise, Lower };

    CliffBrushCommand(int centerVX, int centerVY, int radius, Mode mode);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return "Cliff Brush"; }

private:
    int  centerVX_, centerVY_;
    int  radius_;
    Mode mode_;

    struct CliffRecord {
        int     vx, vy;
        uint8_t oldLevel;
        uint8_t newLevel;
    };
    std::vector<CliffRecord> affected_;
    bool captured_ = false;
};
