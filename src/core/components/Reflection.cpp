#include "Reflection.h"
#include <array>
#include <cstddef>   // offsetof
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// Static registry (Meyers singleton, thread-safe in C++11+)
// ─────────────────────────────────────────────────────────────────────────────

const ComponentMeta& getComponentMeta(ComponentType type)
{
    static const std::array<ComponentMeta, 7> kReg = {
        ComponentMeta{"Transform", ComponentType::Transform, {
            {"x", PropertyType::Float, offsetof(TransformComponent, x), {}},
            {"y", PropertyType::Float, offsetof(TransformComponent, y), {}},
        }},
        ComponentMeta{"Render", ComponentType::Render, {
            {"sprite",  PropertyType::String, offsetof(RenderComponent, sprite),  {}},
            {"layer",   PropertyType::Int,    offsetof(RenderComponent, layer),   {}},
            {"visible", PropertyType::Bool,   offsetof(RenderComponent, visible), {}},
        }},
        ComponentMeta{"Health", ComponentType::Health, {
            {"health",    PropertyType::Int, offsetof(HealthComponent, health),    {}},
            {"maxHealth", PropertyType::Int, offsetof(HealthComponent, maxHealth), {}},
        }},
        ComponentMeta{"Mana", ComponentType::Mana, {
            {"mana",    PropertyType::Int, offsetof(ManaComponent, mana),    {}},
            {"maxMana", PropertyType::Int, offsetof(ManaComponent, maxMana), {}},
        }},
        ComponentMeta{"Stats", ComponentType::Stats, {
            {"attack",      PropertyType::Int,   offsetof(StatsComponent, attack),      {}},
            {"defense",     PropertyType::Int,   offsetof(StatsComponent, defense),     {}},
            {"magicAttack", PropertyType::Int,   offsetof(StatsComponent, magicAttack), {}},
            {"speed",       PropertyType::Int,   offsetof(StatsComponent, speed),       {}},
            {"critChance",  PropertyType::Float, offsetof(StatsComponent, critChance),  {}},
            {"level",       PropertyType::Int,   offsetof(StatsComponent, level),       {}},
        }},
        ComponentMeta{"Combat", ComponentType::Combat, {
            {"attackRange",    PropertyType::Float, offsetof(CombatComponent, attackRange),    {}},
            {"attackCooldown", PropertyType::Float, offsetof(CombatComponent, attackCooldown), {}},
            {"cooldownTimer",  PropertyType::Float, offsetof(CombatComponent, cooldownTimer),  {}},
            {"isAttacking",    PropertyType::Bool,  offsetof(CombatComponent, isAttacking),    {}},
        }},
        ComponentMeta{"AI", ComponentType::AI, {
            {"behavior",       PropertyType::Enum,  offsetof(AIComponent, behavior),       {"Idle","Patrol","Chase","Flee"}},
            {"detectionRange", PropertyType::Float, offsetof(AIComponent, detectionRange), {}},
            {"patrolRadius",   PropertyType::Float, offsetof(AIComponent, patrolRadius),   {}},
        }},
    };
    int idx = static_cast<int>(type);
    if (idx < 0 || idx >= 7)
        throw std::runtime_error("getComponentMeta: unknown ComponentType");
    return kReg[idx];
}

// ─────────────────────────────────────────────────────────────────────────────

ComponentType getVariantType(const ComponentVariant& comp)
{
    return std::visit([](const auto& c) -> ComponentType {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, TransformComponent>) return ComponentType::Transform;
        else if constexpr (std::is_same_v<T, RenderComponent>)  return ComponentType::Render;
        else if constexpr (std::is_same_v<T, HealthComponent>)  return ComponentType::Health;
        else if constexpr (std::is_same_v<T, ManaComponent>)    return ComponentType::Mana;
        else if constexpr (std::is_same_v<T, StatsComponent>)   return ComponentType::Stats;
        else if constexpr (std::is_same_v<T, CombatComponent>)  return ComponentType::Combat;
        else                                                     return ComponentType::AI;
    }, comp);
}

// ─────────────────────────────────────────────────────────────────────────────

void* fieldPtr(ComponentVariant& comp, const PropertyInfo& prop)
{
    return std::visit([offset = prop.offset](auto& c) -> void* {
        return reinterpret_cast<char*>(&c) + offset;
    }, comp);
}

// ─────────────────────────────────────────────────────────────────────────────

PropertyValue readFieldValue(void* ptr, PropertyType type)
{
    switch (type) {
    case PropertyType::Float:  return *static_cast<float*>(ptr);
    case PropertyType::Int:    return *static_cast<int*>(ptr);
    case PropertyType::String: return *static_cast<std::string*>(ptr);
    case PropertyType::Bool:   return *static_cast<bool*>(ptr);
    case PropertyType::Enum:   return *static_cast<int*>(ptr);   // enum : int
    }
    return 0; // unreachable
}

void writeFieldValue(void* ptr, PropertyType type, const PropertyValue& val)
{
    switch (type) {
    case PropertyType::Float:  *static_cast<float*>(ptr)       = std::get<float>(val);       break;
    case PropertyType::Int:    *static_cast<int*>(ptr)         = std::get<int>(val);         break;
    case PropertyType::String: *static_cast<std::string*>(ptr) = std::get<std::string>(val); break;
    case PropertyType::Bool:   *static_cast<bool*>(ptr)        = std::get<bool>(val);        break;
    case PropertyType::Enum:   *static_cast<int*>(ptr)         = std::get<int>(val);         break;
    }
}
