#pragma once
#include "Components.h"
#include <string>
#include <vector>
#include <variant>
#include <cstddef>

// ─────────────────────────────────────────────────────────────────────────────
// PropertyType — type tag for a runtime-reflected field
// ─────────────────────────────────────────────────────────────────────────────
enum class PropertyType { Float, Int, String, Bool, Enum };

// ─────────────────────────────────────────────────────────────────────────────
// PropertyValue — type-erased value used for undo/redo payloads
// (int is also the underlying storage for Enum fields)
// ─────────────────────────────────────────────────────────────────────────────
using PropertyValue = std::variant<int, float, std::string, bool>;

// ─────────────────────────────────────────────────────────────────────────────
// PropertyInfo — metadata for one field of a component
// ─────────────────────────────────────────────────────────────────────────────
struct PropertyInfo {
    std::string              name;
    PropertyType             type;
    std::size_t              offset;
    std::vector<std::string> enumValues; // populated only for Enum
};

// ─────────────────────────────────────────────────────────────────────────────
// ComponentMeta — metadata for one component type
// ─────────────────────────────────────────────────────────────────────────────
struct ComponentMeta {
    std::string               name;
    ComponentType             type;
    std::vector<PropertyInfo> properties;
};

// ─────────────────────────────────────────────────────────────────────────────
// Global registry API
// ─────────────────────────────────────────────────────────────────────────────

/// Return static metadata for the given ComponentType.
const ComponentMeta& getComponentMeta(ComponentType type);

/// Return the ComponentType tag for the active alternative in a variant.
ComponentType getVariantType(const ComponentVariant& comp);

/// Return a raw pointer to the field described by prop inside the variant
/// (uses the stored byte offset from the start of the concrete struct).
void* fieldPtr(ComponentVariant& comp, const PropertyInfo& prop);

/// Read a PropertyValue from a raw field pointer given a PropertyType.
PropertyValue readFieldValue(void* ptr, PropertyType type);

/// Write a PropertyValue to a raw field pointer given a PropertyType.
void writeFieldValue(void* ptr, PropertyType type, const PropertyValue& val);
