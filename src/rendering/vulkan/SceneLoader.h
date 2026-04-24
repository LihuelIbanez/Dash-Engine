#pragma once

#include <string>
#include <vector>

#include "game/physics/PhysicsWorld.h"
#include "rendering/vulkan/RenderTypes.h"

namespace dash::vkexp {

class SceneLoader {
public:
    static bool loadSpawnPoint(const std::string& scenePath, dash::physics::Vec3& outSpawn);

    static std::vector<RenderInstance> loadInstances(const std::string& scenePath);

    static bool loadPlayerPosition(const std::string& scenePath, float& outX, float& outZ);

    static std::vector<RenderInstance> loadTerrainInstances(
        const std::string& scenePath,
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
