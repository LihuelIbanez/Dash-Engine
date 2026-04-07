#pragma once
#include "Entity.h"

// ─────────────────────────────────────────────────────────────────────────────
// CharacterClass – determines base stats and specialisation
// ─────────────────────────────────────────────────────────────────────────────
enum class CharacterClass {
    Warrior,   // High HP / defence, melee
    Mage,      // Low HP, high magic attack
    Rogue,     // Medium HP, high speed / crit chance
    Archer,    // Ranged, balanced speed
    Enemy      // Generic enemy archetype
};

const char* classNameStr(CharacterClass cls);

// ─────────────────────────────────────────────────────────────────────────────
// Stats – RPG combat numbers, easy to extend
// ─────────────────────────────────────────────────────────────────────────────
struct Stats {
    int   attack;          // base physical damage
    int   defense;         // damage reduction
    int   magicAttack;     // magic damage bonus
    float speed;           // world units per second
    float critChance;      // 0.0 – 1.0
    int   level;
    int   experience;
    int   expToNextLevel;

    // Apply class template onto empty stats
    static Stats fromClass(CharacterClass cls);
};

// ─────────────────────────────────────────────────────────────────────────────
// Character – Entity + RPG stats
// ─────────────────────────────────────────────────────────────────────────────
class Character : public Entity {
public:
    CharacterClass charClass;
    Stats          stats;

    // Mana
    int mana    = 0;
    int maxMana = 0;

    // Current desired velocity in world-space (set each frame)
    float vx = 0.f;
    float vy = 0.f;

    // Attack state
    float attackCooldown    = 0.f;  // seconds remaining until next attack
    float attackCooldownMax = 1.0f; // base cooldown (modified by speed)
    bool  isAttacking       = false;

    Character(float x, float y, CharacterClass cls,
              int maxHp, const std::string& name);
    virtual ~Character() = default;

    // --- combat helpers ------------------------------------------------------
    int  rollDamage()              const;
    void gainExperience(int exp);
    bool canAttack()               const { return attackCooldown <= 0.f; }

    // --- update cooldowns (call from derived update) -------------------------
    void tickCooldowns(float dt);

    // --- pure virtuals remain from Entity ------------------------------------
    void update(float dt) override = 0;
    void draw(SDL_Renderer*, float camX, float camY) const override = 0;

protected:
    void levelUp();
};
