#pragma once
#include "Components.h"
#include <nlohmann/json.hpp>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// ComponentSerialization — JSON roundtrip for ComponentVariant
// ─────────────────────────────────────────────────────────────────────────────

/// Serialize a ComponentVariant to JSON (includes "type" string field).
nlohmann::json componentToJson(const ComponentVariant& comp);

/// Deserialize a ComponentVariant from JSON (reads "type" to dispatch).
/// Throws std::runtime_error if the type name is unknown.
ComponentVariant componentFromJson(const nlohmann::json& j);

/// Human-readable name for a ComponentType value (e.g., "Transform").
std::string componentTypeName(ComponentType type);

/// Reverse mapping: name → ComponentType.
/// Throws std::runtime_error if the name is unknown.
ComponentType componentTypeFromName(const std::string& name);
