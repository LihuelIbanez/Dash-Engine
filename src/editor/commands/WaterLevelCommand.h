#pragma once
#include "ICommand.h"
#include "SceneData.h"
#include "World.h"
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// WaterLevelCommand – adjust water body level (WC3-style water planes)
// ─────────────────────────────────────────────────────────────────────────────
class WaterLevelCommand : public ICommand {
public:
    WaterLevelCommand(uint8_t bodyId, float oldLevel, float newLevel);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return "Water Level"; }

private:
    uint8_t bodyId_;
    float   oldLevel_;
    float   newLevel_;
};
