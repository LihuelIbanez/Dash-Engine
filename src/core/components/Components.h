#pragma once
#include <string>
#include <variant>

// ─────────────────────────────────────────────────────────────────────────────
// ComponentType — stable numeric IDs (never reorder; used for serialization)
// ─────────────────────────────────────────────────────────────────────────────
enum class ComponentType : int {
    Transform = 0,
    Render    = 1,
    Health    = 2,
    Mana      = 3,
    Stats     = 4,
    Combat    = 5,
    AI        = 6,
};

// ─────────────────────────────────────────────────────────────────────────────
// Component structs — plain data, zero logic, all fields have defaults
// ─────────────────────────────────────────────────────────────────────────────

struct TransformComponent {
    float x = 0.f;
    float y = 0.f;
};

struct RenderComponent {
    std::string sprite  = "default";
    int         layer   = 0;
    bool        visible = true;
};

struct HealthComponent {
    int health    = 100;
    int maxHealth = 100;
};

struct ManaComponent {
    int mana    = 50;
    int maxMana = 50;
};

struct StatsComponent {
    int   attack        = 10;
    int   defense       = 5;
    int   magicAttack   = 0;
    int   speed         = 3;
    float critChance    = 0.05f;
    int   level         = 1;
    int   experience    = 0;
    int   expToNextLevel = 100;
};

struct CombatComponent {
    float attackRange    = 1.8f;
    float attackCooldown = 0.8f;
    float cooldownTimer  = 0.f;
    bool  isAttacking    = false;
};

struct AIComponent {
    enum class Behavior : int { Idle = 0, Patrol = 1, Chase = 2, Flee = 3 };
    Behavior behavior       = Behavior::Idle;
    float    detectionRange = 5.f;
    float    patrolRadius   = 3.f;
};

// ─────────────────────────────────────────────────────────────────────────────
// ComponentVariant — type-safe union of all component types
// ─────────────────────────────────────────────────────────────────────────────
using ComponentVariant = std::variant<
    TransformComponent,
    RenderComponent,
    HealthComponent,
    ManaComponent,
    StatsComponent,
    CombatComponent,
    AIComponent
>;
