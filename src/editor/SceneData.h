#pragma once
#include <string>
#include <vector>
#include "IsoRenderer.h"
#include "World.h"

// ─────────────────────────────────────────────────────────────────────────────
// EntityData – serialisable description of one entity in a scene
// ─────────────────────────────────────────────────────────────────────────────
struct EntityData {
    enum class Type { Player, Enemy };
    Type        type      = Type::Enemy;
    std::string name      = "Enemy";
    float       x         = 0.f;
    float       y         = 0.f;
    std::string charClass = "Warrior";   // only used when type == Player
};

// ─────────────────────────────────────────────────────────────────────────────
// TileOverride – a single tile whose type differs from the generated base
// ─────────────────────────────────────────────────────────────────────────────
struct TileOverride {
    int  x, y;
    int  tileType;   // cast to/from TileType
    bool walkable;
};

// ─────────────────────────────────────────────────────────────────────────────
// SceneData – everything needed to describe one scenario / level
// ─────────────────────────────────────────────────────────────────────────────
struct SceneData {
    std::string  sceneName = "Untitled";
    unsigned int worldSeed = 12345;

    std::vector<TileOverride> tileOverrides;
    std::vector<EntityData>   entities;

    std::string filePath;
    bool        modified = false;

    void createDefault();
    bool saveToFile(const std::string& path);
    bool loadFromFile(const std::string& path);
};
