#pragma once
#include "ISystem.h"
#include <memory>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// SystemScheduler – runs systems in registration order each frame
// ─────────────────────────────────────────────────────────────────────────────
class SystemScheduler {
public:
    void addSystem(std::unique_ptr<ISystem> system);
    void updateAll(RuntimeContext& ctx);
    size_t systemCount() const { return systems_.size(); }

private:
    std::vector<std::unique_ptr<ISystem>> systems_;
};
