#pragma once
#include "ISystem.h"

// ─────────────────────────────────────────────────────────────────────────────
// CombatSystem – resolve attacks from player and enemies
// ─────────────────────────────────────────────────────────────────────────────
class CombatSystem : public ISystem {
public:
    void update(RuntimeContext& ctx) override;
    const char* name() const override { return "Combat"; }
};
