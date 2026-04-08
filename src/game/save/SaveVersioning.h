#pragma once
#include <nlohmann/json_fwd.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// SaveVersioning – migrate old savegame JSON to the current format
// ─────────────────────────────────────────────────────────────────────────────
namespace SaveVersioning {

// Current save format version (must match SaveData::kCurrentVersion).
constexpr int kLatestVersion = 1;

// Migrate a savegame JSON from `fromVersion` up to kLatestVersion.
// Modifies `j` in-place. Returns false if migration is not possible.
bool migrate(nlohmann::json& j, int fromVersion);

}  // namespace SaveVersioning
