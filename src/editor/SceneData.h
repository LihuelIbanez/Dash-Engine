#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "IsoRenderer.h"
#include "World.h"
#include "components/Components.h"
#include <nlohmann/json.hpp>

// ─────────────────────────────────────────────────────────────────────────────
// EntityData – serialisable description of one entity in a scene
// ─────────────────────────────────────────────────────────────────────────────
struct EntityData {
    enum class Type { Player, Enemy };

    uint64_t    id        = 0;           // stable entity identifier
    Type        type      = Type::Enemy;
    std::string name      = "Enemy";
    float       x         = 0.f;
    float       y         = 0.f;
    std::string charClass = "Warrior";   // only used when type == Player

    // v2+: component data (empty = legacy/not migrated yet)
    std::vector<ComponentVariant> components;

    // Prefab instance support: if non-empty, entity originates from a prefab.
    std::string        prefabGuid;       // empty = not a prefab instance
    nlohmann::json     componentOverrides; // diff vs prefab defaults
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
    static constexpr int kCurrentVersion = 2;

    std::string  sceneName    = "Untitled";
    unsigned int worldSeed    = 12345;
    int          sceneVersion = kCurrentVersion;

    std::vector<TileOverride> tileOverrides;
    std::vector<EntityData>   entities;

    uint64_t    nextEntityId = 1;        // monotonic ID generator

    std::string filePath;
    bool        modified = false;

    // Last load/validation errors (empty = OK)
    std::vector<std::string> loadErrors;

    uint64_t allocateEntityId();
    void createDefault();
    bool saveToFile(const std::string& path);
    bool loadFromFile(const std::string& path,
                      const std::string& assetsRoot = "");
};
