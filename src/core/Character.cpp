#include "Character.h"
#include <cstdlib>   // rand
#include <cmath>
#include <algorithm>

// ─── Stat templates per class ─────────────────────────────────────────────────
Stats Stats::fromClass(CharacterClass cls)
{
    switch (cls) {
    case CharacterClass::Warrior:
        return { 18, 10, 2,  3.0f, 0.05f, 1, 0, 100 };
    case CharacterClass::Mage:
        return {  8,  4, 22, 3.5f, 0.08f, 1, 0, 100 };
    case CharacterClass::Rogue:
        return { 14,  6,  4, 5.0f, 0.20f, 1, 0, 100 };
    case CharacterClass::Archer:
        return { 12,  7,  5, 4.5f, 0.12f, 1, 0, 100 };
    case CharacterClass::Enemy:
        return { 10,  5,  0, 2.5f, 0.05f, 1, 0, 100 };
    }
    return { 10, 5, 0, 3.0f, 0.05f, 1, 0, 100 };
}

const char* classNameStr(CharacterClass cls)
{
    switch (cls) {
    case CharacterClass::Warrior: return "Warrior";
    case CharacterClass::Mage:    return "Mage";
    case CharacterClass::Rogue:   return "Rogue";
    case CharacterClass::Archer:  return "Archer";
    case CharacterClass::Enemy:   return "Enemy";
    }
    return "Unknown";
}

// ─── Character ────────────────────────────────────────────────────────────────
// Mana per class
static int classMana(CharacterClass cls)
{
    switch (cls) {
    case CharacterClass::Warrior: return  60;
    case CharacterClass::Mage:    return 160;
    case CharacterClass::Rogue:   return  80;
    case CharacterClass::Archer:  return 100;
    default:                      return  50;
    }
}

Character::Character(float x, float y, CharacterClass cls,
                     int maxHp, const std::string& name)
    : Entity(x, y, maxHp, name)
    , charClass(cls)
    , stats(Stats::fromClass(cls))
{
    // Faster characters attack a bit more quickly
    attackCooldownMax = 1.0f / (0.5f + stats.speed * 0.1f);
    maxMana = classMana(cls);
    mana    = maxMana;
}

void Character::tickCooldowns(float dt)
{
    if (attackCooldown > 0.f)
        attackCooldown = std::max(0.f, attackCooldown - dt);
    isAttacking = false;

    // Slow mana regeneration (2 mana/sec)
    if (mana < maxMana) {
        mana = std::min(maxMana, mana + static_cast<int>(2.f * dt + 0.5f));
    }
}

int Character::rollDamage() const
{
    int base = stats.attack + stats.magicAttack;
    // Variance ±20 %
    int variance = static_cast<int>(base * 0.2f);
    int dmg = base + (variance > 0 ? (std::rand() % (variance * 2 + 1) - variance) : 0);
    // Crit
    float r = static_cast<float>(std::rand()) / RAND_MAX;
    if (r < stats.critChance) dmg = static_cast<int>(dmg * 1.75f);
    return std::max(1, dmg);
}

void Character::gainExperience(int exp)
{
    stats.experience += exp;
    while (stats.experience >= stats.expToNextLevel)
        levelUp();
}

void Character::levelUp()
{
    stats.experience   -= stats.expToNextLevel;
    stats.level        += 1;
    stats.expToNextLevel = static_cast<int>(stats.expToNextLevel * 1.4f);

    // Scale stats
    stats.attack  += 3;
    stats.defense += 2;
    maxHealth     += 20;
    maxMana       += 15;
    health         = maxHealth;   // full heal on level-up
    mana           = maxMana;
}
