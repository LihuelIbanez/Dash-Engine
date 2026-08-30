#pragma once

#include <string>
#include <vector>

#include "rendering/vulkan/RenderTypes.h"
#include "rendering/vulkan/SceneLoader.h"
#include "input/InputBindings3D.h"

struct GLFWwindow;
class TerrainMesh;

namespace dash::vkexp {

class PlayerController {
public:
    // Load player position from the parsed scene.
    // Uses terrain data for initial height placement.
    bool loadFromScene(const LoadedScene& scene,
                       const TerrainMesh* terrainMesh, bool terrainMeshReady,
                       const std::vector<float>& heightMap,
                       int mapWidth, int mapHeight);

    // Process WASD input and apply gravity against terrain.
    void update(GLFWwindow* window, const InputBindings3D& bindings, float dt,
                const TerrainMesh* terrainMesh, bool terrainMeshReady,
                const std::vector<float>& heightMap,
                int mapWidth, int mapHeight);

    // Sync the player's position into the matching scene render instance.
    void syncToInstances(std::vector<RenderInstance>& instances) const;

    bool isLoaded() const { return loaded_; }
    float x() const { return x_; }
    float y() const { return y_; }
    float z() const { return z_; }
    // Attack binding state as of the last update(). Held rather than edge
    // triggered: the combat cooldown is what paces the swings.
    bool attackHeld() const { return attackHeld_; }

private:
    float x_ = 32.0f;
    float y_ = 1.0f;
    float z_ = 32.0f;
    float velY_ = 0.0f;
    bool loaded_ = false;
    bool attackHeld_ = false;
};

} // namespace dash::vkexp
