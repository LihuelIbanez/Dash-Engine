#include "SaveVersioning.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Version migration chain
//
// Each function migrates from version N to N+1. Add new migration steps as
// the save format evolves.
// ─────────────────────────────────────────────────────────────────────────────

// Example: migrate v1 → v2 (placeholder for future changes)
// static void migrateV1toV2(json& j)
// {
//     // Add new fields with defaults, rename keys, etc.
//     j["someNewField"] = "defaultValue";
//     j["saveVersion"] = 2;
// }

bool SaveVersioning::migrate(json& j, int fromVersion)
{
    if (fromVersion > kLatestVersion) return false;   // unknown future version
    if (fromVersion < 1) return false;                // too old / corrupt

    // Apply migration steps sequentially
    // int v = fromVersion;
    // if (v == 1) { migrateV1toV2(j); ++v; }
    // if (v == 2) { migrateV2toV3(j); ++v; }
    // ...

    // Currently at v1 — nothing to migrate yet.
    j["saveVersion"] = kLatestVersion;
    return true;
}
