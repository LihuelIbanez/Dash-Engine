#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// SaveGame – serialisable snapshot of a game session
// ─────────────────────────────────────────────────────────────────────────────

struct SavePlayerData {
    float       x            = 0.f;
    float       y            = 0.f;
    int         health       = 100;
    int         maxHealth    = 100;
    int         mana         = 50;
    int         maxMana      = 50;
    int         attack       = 10;
    int         defense      = 5;
    int         magicAttack  = 0;
    float       speed        = 3.f;
    float       critChance   = 0.05f;
    int         level        = 1;
    int         experience   = 0;
    int         expToNext    = 100;
    float       atkCooldownMax = 1.f;
    std::string charClass    = "Warrior";
};

struct SaveEnemyData {
    float       x            = 0.f;
    float       y            = 0.f;
    int         health       = 60;
    int         maxHealth    = 60;
    bool        alive        = true;
    std::string name         = "Enemy";
    int         attack       = 10;
    int         defense      = 5;
    int         magicAttack  = 0;
    float       speed        = 2.5f;
    float       critChance   = 0.05f;
    float       detectionRadius = 6.f;
    float       attackRadius = 1.2f;
    int         expReward    = 40;
    float       atkCooldownMax = 1.f;
};

struct SaveData {
    static constexpr int kCurrentVersion = 1;

    int          saveVersion = kCurrentVersion;
    unsigned int worldSeed   = 12345;
    int          score       = 0;

    SavePlayerData              player;
    std::vector<SaveEnemyData>  enemies;
};

// ─────────────────────────────────────────────────────────────────────────────
// SaveGame – read / write savegame files (JSON)
// ─────────────────────────────────────────────────────────────────────────────
namespace SaveGame {

// Write a SaveData to disk. Returns true on success.
bool save(const SaveData& data, const std::string& path);

// Read a SaveData from disk. Returns true on success.
// On failure, `out` is left unchanged.
bool load(const std::string& path, SaveData& out);

}  // namespace SaveGame
