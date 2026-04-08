#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Game events — plain data structs, no logic.
// Emitted by systems and delivered via EventDispatcher at end of frame.
// ─────────────────────────────────────────────────────────────────────────────

// Emitted every time an attack deals damage to a target.
struct DamageEvent {
    uint64_t    attackerId  = 0;     // 0 = anonymous / not tracked
    uint64_t    targetId    = 0;
    std::string targetName;
    int         damage      = 0;
    int         finalHealth = 0;     // target's health *after* damage
};

// Emitted when an entity's health reaches zero.
struct DeathEvent {
    uint64_t    entityId   = 0;
    float       x          = 0.f;
    float       y          = 0.f;
    std::string entityName;
    int         expReward  = 0;      // XP value of the killed entity
};

// Emitted when a character gains a level.
struct LevelUpEvent {
    int oldLevel  = 0;
    int newLevel  = 0;
    int totalExp  = 0;
};

// Emitted whenever an entity's health changes (damage or healing).
struct HealthChangeEvent {
    uint64_t entityId   = 0;
    int      oldHealth  = 0;
    int      newHealth  = 0;
    int      maxHealth  = 0;
};

// Emitted when an enemy drops loot on death.
struct LootDropEvent {
    std::string enemyId;          // lowercase enemy id (e.g. "skeleton")
    float       x = 0.f;
    float       y = 0.f;
    struct DroppedItem {
        std::string item;
        int         qty = 0;
    };
    std::vector<DroppedItem> items;
};

