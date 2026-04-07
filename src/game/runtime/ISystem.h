#pragma once
#include "RuntimeContext.h"

// ─────────────────────────────────────────────────────────────────────────────
// ISystem – base interface for all runtime gameplay systems
// ─────────────────────────────────────────────────────────────────────────────
class ISystem {
public:
    virtual ~ISystem() = default;
    virtual void update(RuntimeContext& ctx) = 0;
    virtual const char* name() const = 0;
};
