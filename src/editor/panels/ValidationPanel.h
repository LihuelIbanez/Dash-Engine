#pragma once
#include "ContentValidator.h"
#include <cstdint>
#include <functional>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// ValidationPanel — shows validation issues; clicking navigates to the entity.
// Caller must provide a refresh callback so the panel can re-validate.
// ─────────────────────────────────────────────────────────────────────────────
class ValidationPanel {
public:
    using RefreshCallback = std::function<void()>;

    void draw(const std::vector<ValidationIssue>& issues,
              uint64_t& selectedEntityId,
              float& camX,
              float& camY,
              RefreshCallback refreshCb = nullptr);
};
