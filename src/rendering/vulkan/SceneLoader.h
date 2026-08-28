#pragma once

#include <string>
#include <vector>

#include "SceneData.h"
#include "game/physics/PhysicsWorld.h"
#include "rendering/vulkan/RenderTypes.h"

namespace dash::vkexp {

// A scene file parsed exactly once: the versioned typed scene plus the
// runtime-only tilemap the editor appends when exporting for Play / Build & Run.
struct LoadedScene {
    SceneData data;
    std::vector<int> tilemap;
    int worldWidth = 0;
    int worldHeight = 0;
    bool valid = false;
};

class SceneLoader {
public:
    static LoadedScene load(const std::string& scenePath);

    static bool loadSpawnPoint(const LoadedScene& scene, dash::physics::Vec3& outSpawn);

    static std::vector<RenderInstance> loadInstances(const LoadedScene& scene);

    // Same conversion, straight from SceneData. The editor viewport uses this
    // so its instances match the runtime's field for field.
    static std::vector<RenderInstance> buildInstances(const SceneData& data);

    // Physics bodies declared by entities carrying a PhysicsComponent.
    static std::vector<PhysicsSpawn> loadPhysicsBodies(const LoadedScene& scene);

    static bool loadPlayerPosition(const LoadedScene& scene, float& outX, float& outZ);

    static std::vector<RenderInstance> loadTerrainInstances(
        const LoadedScene& scene,
        std::vector<float>* outHeightMap,
        int* outMapWidth,
        int* outMapHeight);

    static float sampleTerrainHeight(
        const std::vector<float>& mapHeights,
        int mapWidth, int mapHeight,
        float x, float z);

    static dash::physics::Vec3 getTileColor(int tileType);
    static float getTileHeight(int tileType);
};

} // namespace dash::vkexp
