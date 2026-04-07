#pragma once
#include "ISystem.h"

// ─────────────────────────────────────────────────────────────────────────────
// AISystem – enemy state machine + auto-attack trigger
// ─────────────────────────────────────────────────────────────────────────────
class AISystem : public ISystem {
public:
    void update(RuntimeContext& ctx) override;
    const char* name() const override { return "AI"; }
};
