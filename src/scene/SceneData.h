#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include "IsoRenderer.h"
#include "World.h"
#include "TerrainMesh.h"
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

    // v7+: parent for hierarchical transforms. 0 = root.
    uint64_t    parentId  = 0;

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
// VertexHeightOverride – a vertex whose height differs from procedural baseline
// ─────────────────────────────────────────────────────────────────────────────
struct VertexHeightOverride {
    int   vx, vy;
    float height;
};

// ─────────────────────────────────────────────────────────────────────────────
// CliffOverride – a vertex whose cliff level differs from default (0)
// ─────────────────────────────────────────────────────────────────────────────
struct CliffOverride {
    int     vx, vy;
    uint8_t cliffLevel;
};

// ─────────────────────────────────────────────────────────────────────────────
// TextureOverride – a vertex whose texture blend differs from default
// ─────────────────────────────────────────────────────────────────────────────
struct TextureOverride {
    int     vx, vy;
    uint8_t texIndices[4];
    uint8_t texWeights[4];
};

// ─────────────────────────────────────────────────────────────────────────────
// SceneData – everything needed to describe one scenario / level
// ─────────────────────────────────────────────────────────────────────────────
struct SceneData {
    // v7 adds entity parenting (EntityData::parentId) plus the Light and
    // Animation components. All are optional, so older scenes load unchanged:
    // absent parentId means root and absent components mean no light/animation.
    static constexpr int kCurrentVersion = 7;

    struct Render3DSettings {
        bool  useVulkan3D = true;
        bool  embeddedPreview = false;
        float isoYawDeg = 45.0f;
        float isoPitchDeg = 35.264f;
        float cameraDistance = 8.0f;
        float cameraHeight = 2.5f;
        float zoom = 1.0f;
        float heightScale = 32.0f;
        float gridOpacity = 0.22f;
    };

    std::string  sceneName    = "Untitled";
    unsigned int worldSeed    = 12345;
    int          sceneVersion = kCurrentVersion;
    Render3DSettings render3d;

    std::vector<TileOverride> tileOverrides;
    std::vector<VertexHeightOverride> vertexHeightOverrides;
    std::vector<CliffOverride> cliffOverrides;
    std::vector<TextureOverride> textureOverrides;
    std::vector<WaterBody> waterBodies;
    std::vector<EntityData>   entities;

    uint64_t    nextEntityId = 1;        // monotonic ID generator

    std::string filePath;
    bool        modified = false;

    // Last load/validation errors (empty = OK)
    std::vector<std::string> loadErrors;

    uint64_t allocateEntityId();
    void createDefault();
    nlohmann::json toJson() const;
    bool saveToJsonString(std::string& outJson) const;
    bool loadFromJson(const nlohmann::json& j,
                      const std::string& assetsRoot = "");
    bool loadFromJsonString(const std::string& rawJson,
                            const std::string& assetsRoot = "");
    bool saveToFile(const std::string& path);
    bool loadFromFile(const std::string& path,
                      const std::string& assetsRoot = "");
};
