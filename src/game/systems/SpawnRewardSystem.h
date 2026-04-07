#pragma once
#include "ISystem.h"

// ─────────────────────────────────────────────────────────────────────────────
// SpawnRewardSystem – grant XP, remove dead enemies, update score
// ─────────────────────────────────────────────────────────────────────────────
class SpawnRewardSystem : public ISystem {
public:
    void update(RuntimeContext& ctx) override;
    const char* name() const override { return "SpawnReward"; }
};
