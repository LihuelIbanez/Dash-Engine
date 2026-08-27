#include "ComponentSerialization.h"
#include <stdexcept>
#include <string>

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Type name registry
// ─────────────────────────────────────────────────────────────────────────────

std::string componentTypeName(ComponentType type)
{
    switch (type) {
        case ComponentType::Transform: return "Transform";
        case ComponentType::Render:    return "Render";
        case ComponentType::Health:    return "Health";
        case ComponentType::Mana:      return "Mana";
        case ComponentType::Stats:     return "Stats";
        case ComponentType::Combat:    return "Combat";
        case ComponentType::AI:        return "AI";
        case ComponentType::Physics:   return "Physics";
        default:                       return "Unknown";
    }
}

ComponentType componentTypeFromName(const std::string& name)
{
    if (name == "Transform") return ComponentType::Transform;
    if (name == "Render")    return ComponentType::Render;
    if (name == "Health")    return ComponentType::Health;
    if (name == "Mana")      return ComponentType::Mana;
    if (name == "Stats")     return ComponentType::Stats;
    if (name == "Combat")    return ComponentType::Combat;
    if (name == "AI")        return ComponentType::AI;
    if (name == "Physics")   return ComponentType::Physics;
    throw std::runtime_error("Unknown component type: " + name);
}

// ─────────────────────────────────────────────────────────────────────────────
// AIComponent::Behavior helpers
// ─────────────────────────────────────────────────────────────────────────────

static const char* behaviorName(AIComponent::Behavior b)
{
    switch (b) {
        case AIComponent::Behavior::Idle:   return "Idle";
        case AIComponent::Behavior::Patrol: return "Patrol";
        case AIComponent::Behavior::Chase:  return "Chase";
        case AIComponent::Behavior::Flee:   return "Flee";
        default:                            return "Idle";
    }
}

static AIComponent::Behavior behaviorFromName(const std::string& s)
{
    if (s == "Patrol") return AIComponent::Behavior::Patrol;
    if (s == "Chase")  return AIComponent::Behavior::Chase;
    if (s == "Flee")   return AIComponent::Behavior::Flee;
    return AIComponent::Behavior::Idle;
}

// ─────────────────────────────────────────────────────────────────────────────
// componentToJson
// ─────────────────────────────────────────────────────────────────────────────

nlohmann::json componentToJson(const ComponentVariant& comp)
{
    json j;
    std::visit([&](auto&& c) {
        using T = std::decay_t<decltype(c)>;

        if constexpr (std::is_same_v<T, TransformComponent>) {
            j["type"] = "Transform";
            j["x"]    = c.x;
            j["y"]    = c.y;
            j["z"]    = c.z;
            j["yawDeg"] = c.yawDeg;
            j["pitchDeg"] = c.pitchDeg;
            j["rollDeg"] = c.rollDeg;
            j["scale"] = c.scale;
        }
        else if constexpr (std::is_same_v<T, RenderComponent>) {
            j["type"]    = "Render";
            j["renderMode"] = c.renderMode;
            j["mesh"]    = c.mesh;
            j["material"] = c.material;
            j["sprite"]  = c.sprite;
            j["layer"]   = c.layer;
            j["visible"] = c.visible;
        }
        else if constexpr (std::is_same_v<T, HealthComponent>) {
            j["type"]      = "Health";
            j["health"]    = c.health;
            j["maxHealth"] = c.maxHealth;
        }
        else if constexpr (std::is_same_v<T, ManaComponent>) {
            j["type"]    = "Mana";
            j["mana"]    = c.mana;
            j["maxMana"] = c.maxMana;
        }
        else if constexpr (std::is_same_v<T, StatsComponent>) {
            j["type"]          = "Stats";
            j["attack"]        = c.attack;
            j["defense"]       = c.defense;
            j["magicAttack"]   = c.magicAttack;
            j["speed"]         = c.speed;
            j["critChance"]    = c.critChance;
            j["level"]         = c.level;
            j["experience"]    = c.experience;
            j["expToNextLevel"] = c.expToNextLevel;
        }
        else if constexpr (std::is_same_v<T, CombatComponent>) {
            j["type"]            = "Combat";
            j["attackRange"]     = c.attackRange;
            j["attackCooldown"]  = c.attackCooldown;
            j["cooldownTimer"]   = c.cooldownTimer;
            j["isAttacking"]     = c.isAttacking;
        }
        else if constexpr (std::is_same_v<T, AIComponent>) {
            j["type"]           = "AI";
            j["behavior"]       = behaviorName(c.behavior);
            j["detectionRange"] = c.detectionRange;
            j["patrolRadius"]   = c.patrolRadius;
        }
        else if constexpr (std::is_same_v<T, PhysicsComponent>) {
            j["type"]        = "Physics";
            j["shape"]       = c.shape;
            j["halfExtentX"] = c.halfExtentX;
            j["halfExtentY"] = c.halfExtentY;
            j["halfExtentZ"] = c.halfExtentZ;
            j["mass"]        = c.mass;
            j["isStatic"]    = c.isStatic;
        }
    }, comp);
    return j;
}

// ─────────────────────────────────────────────────────────────────────────────
// componentFromJson
// ─────────────────────────────────────────────────────────────────────────────

ComponentVariant componentFromJson(const nlohmann::json& j)
{
    std::string typeName = j.value("type", "");
    ComponentType type = componentTypeFromName(typeName); // throws if unknown

    switch (type) {
        case ComponentType::Transform: {
            TransformComponent c;
            c.x = j.value("x", 0.f);
            c.y = j.value("y", 0.f);
            c.z = j.value("z", 0.f);
            c.yawDeg = j.value("yawDeg", 0.f);
            c.pitchDeg = j.value("pitchDeg", 0.f);
            c.rollDeg = j.value("rollDeg", 0.f);
            c.scale = j.value("scale", 1.f);
            return c;
        }
        case ComponentType::Render: {
            RenderComponent c;
            c.renderMode = j.value("renderMode", static_cast<int>(RenderMode::Mesh3D));
            c.mesh    = j.value("mesh", "cube");
            c.material = j.value("material", "default");
            c.sprite  = j.value("sprite", "default");
            c.layer   = j.value("layer", 0);
            c.visible = j.value("visible", true);
            return c;
        }
        case ComponentType::Health: {
            HealthComponent c;
            c.health    = j.value("health", 100);
            c.maxHealth = j.value("maxHealth", 100);
            return c;
        }
        case ComponentType::Mana: {
            ManaComponent c;
            c.mana    = j.value("mana", 50);
            c.maxMana = j.value("maxMana", 50);
            return c;
        }
        case ComponentType::Stats: {
            StatsComponent c;
            c.attack        = j.value("attack", 10);
            c.defense       = j.value("defense", 5);
            c.magicAttack   = j.value("magicAttack", 0);
            c.speed         = j.value("speed", 3);
            c.critChance    = j.value("critChance", 0.05f);
            c.level         = j.value("level", 1);
            c.experience    = j.value("experience", 0);
            c.expToNextLevel = j.value("expToNextLevel", 100);
            return c;
        }
        case ComponentType::Combat: {
            CombatComponent c;
            c.attackRange    = j.value("attackRange", 1.8f);
            c.attackCooldown = j.value("attackCooldown", 0.8f);
            c.cooldownTimer  = j.value("cooldownTimer", 0.f);
            c.isAttacking    = j.value("isAttacking", false);
            return c;
        }
        case ComponentType::AI: {
            AIComponent c;
            c.behavior       = behaviorFromName(j.value("behavior", "Idle"));
            c.detectionRange = j.value("detectionRange", 5.f);
            c.patrolRadius   = j.value("patrolRadius", 3.f);
            return c;
        }
        case ComponentType::Physics: {
            PhysicsComponent c;
            c.shape       = j.value("shape", static_cast<int>(ColliderShape::Box));
            c.halfExtentX = j.value("halfExtentX", 0.3f);
            c.halfExtentY = j.value("halfExtentY", 0.3f);
            c.halfExtentZ = j.value("halfExtentZ", 0.3f);
            c.mass        = j.value("mass", 1.f);
            c.isStatic    = j.value("isStatic", false);
            return c;
        }
        default:
            throw std::runtime_error("Unhandled component type in componentFromJson");
    }
}
