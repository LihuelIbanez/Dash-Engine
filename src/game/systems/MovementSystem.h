#pragma once
#include "ISystem.h"

// ─────────────────────────────────────────────────────────────────────────────
// MovementSystem – player input + movement update
// ─────────────────────────────────────────────────────────────────────────────
class MovementSystem : public ISystem {
public:
    void update(RuntimeContext& ctx) override;
    const char* name() const override { return "Movement"; }
};
