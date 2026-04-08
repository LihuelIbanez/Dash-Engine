#pragma once
#include "Components.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// PrefabAsset – a reusable entity template stored as a JSON file.
// Instances in a scene carry only overrides (diffs) vs the base prefab.
// ─────────────────────────────────────────────────────────────────────────────
struct PrefabAsset {
    std::string guid;                              // stable unique ID
    std::string name;                              // human-readable name
    std::vector<ComponentVariant> defaultComponents; // canonical defaults
};

// Load a prefab from a JSON file. Returns an asset with empty guid on failure.
PrefabAsset loadPrefab(const std::string& path);

// Save a prefab to a JSON file. Returns false on failure.
bool savePrefab(const PrefabAsset& prefab, const std::string& path);

// Return a deep copy of the prefab's default components.
std::vector<ComponentVariant> instantiate(const PrefabAsset& prefab);

// Compute a minimal override JSON describing which fields in `instance` differ
// from the prefab defaults.
// Format: { "modified": { "TypeName": {fieldDiffs} },
//           "added":    [ full_component_json, ... ],
//           "removed":  [ "TypeName", ... ] }
nlohmann::json computeOverrides(const PrefabAsset& prefab,
                                const std::vector<ComponentVariant>& instance);

// Starting from a fresh instantiate() of `prefab`, apply the override JSON
// produced by computeOverrides() and store the result in `instance`.
void applyOverrides(const PrefabAsset& prefab,
                    std::vector<ComponentVariant>& instance,
                    const nlohmann::json& overrides);

// Scan a directory for .json prefab files and return the one whose guid
// matches. Returns empty (guid == "") if not found.
PrefabAsset findPrefabByGuid(const std::string& prefabsDir,
                              const std::string& guid);
