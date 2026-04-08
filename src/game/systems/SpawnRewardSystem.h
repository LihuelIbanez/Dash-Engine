#pragma once
#include "ISystem.h"
#include "GameplayDatabase.h"

// ─────────────────────────────────────────────────────────────────────────────
// SpawnRewardSystem – grant XP, remove dead enemies, update score, emit loot
// ─────────────────────────────────────────────────────────────────────────────
class SpawnRewardSystem : public ISystem {
public:
    // Optional GameplayDatabase for loot table lookups.
    explicit SpawnRewardSystem(const GameplayDatabase* db = nullptr) : db_(db) {}

    void update(RuntimeContext& ctx) override;
    const char* name() const override { return "SpawnReward"; }

private:
    const GameplayDatabase* db_ = nullptr;
};
