#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SceneData;
class World;
class AssetDatabase;

// ─────────────────────────────────────────────────────────────────────────────
// ValidationIssue — one detected problem in a scene
// ─────────────────────────────────────────────────────────────────────────────
struct ValidationIssue {
    enum class Severity { Warning, Error };

    Severity    severity  = Severity::Error;
    std::string message;
    uint64_t    entityId  = 0;    // 0 = not tied to a specific entity
    int         tileX     = -1;   // -1 = not tied to a specific tile
    int         tileY     = -1;
};

// ─────────────────────────────────────────────────────────────────────────────
// ContentValidator — stateless scene auditor
// ─────────────────────────────────────────────────────────────────────────────
class ContentValidator {
public:
    std::vector<ValidationIssue> validate(const SceneData& scene,
                                          const World& world,
                                          const AssetDatabase& db) const;
};
