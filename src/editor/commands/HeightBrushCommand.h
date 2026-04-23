#pragma once
#include "ICommand.h"
#include "SceneData.h"
#include "World.h"
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// HeightBrushCommand – sculpt terrain vertex heights, storing old values for undo
// ─────────────────────────────────────────────────────────────────────────────
class HeightBrushCommand : public ICommand {
public:
    enum class Mode { Raise, Lower, Smooth, Flatten };

    HeightBrushCommand(int centerVX, int centerVY, int radius,
                       float strength, Mode mode);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return "Height Brush"; }

private:
    int   centerVX_, centerVY_;
    int   radius_;
    float strength_;
    Mode  mode_;

    struct VertRecord {
        int   vx, vy;
        float oldHeight;
        float newHeight;
    };
    std::vector<VertRecord> affected_;
    bool captured_ = false;
};
