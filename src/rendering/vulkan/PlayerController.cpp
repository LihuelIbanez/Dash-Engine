#include "rendering/vulkan/PlayerController.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <GLFW/glfw3.h>

#include "world/TerrainMesh.h"

namespace dash::vkexp {

static constexpr float kPlayerHalfHeight = 0.52f;

bool PlayerController::loadFromScene(const std::string& scenePath,
                                      const TerrainMesh* terrainMesh, bool terrainMeshReady,
                                      const std::vector<float>& heightMap,
                                      int mapWidth, int mapHeight)
{
    if (SceneLoader::loadPlayerPosition(scenePath, x_, z_)) {
        loaded_ = true;
        if (terrainMeshReady && terrainMesh) {
            y_ = terrainMesh->sampleHeight(x_, z_) + kPlayerHalfHeight;
        } else {
            y_ = SceneLoader::sampleTerrainHeight(heightMap, mapWidth, mapHeight, x_, z_) + kPlayerHalfHeight;
        }
        velY_ = 0.0f;
        std::fprintf(stderr, "[VSTEP] Player position loaded: (%.2f, %.2f)\n", x_, z_);
        return true;
    }

    loaded_ = false;
    x_ = 32.0f;
    z_ = 32.0f;
    y_ = 1.0f;
    velY_ = 0.0f;
    std::fprintf(stderr, "[VSTEP] Player position not found, using default (32, 32)\n");
    return false;
}

void PlayerController::update(GLFWwindow* window, const InputBindings3D& bindings, float dt,
                               const TerrainMesh* terrainMesh, bool terrainMeshReady,
                               const std::vector<float>& heightMap,
                               int mapWidth, int mapHeight)
{
    if (!loaded_) return;

    float inputX = 0.0f;
    float inputZ = 0.0f;
    if (glfwGetKey(window, bindings.keyForward) == GLFW_PRESS) {
        inputX -= 1.0f;
        inputZ -= 1.0f;
    }
    if (glfwGetKey(window, bindings.keyBackward) == GLFW_PRESS) {
        inputX += 1.0f;
        inputZ += 1.0f;
    }
    if (glfwGetKey(window, bindings.keyLeft) == GLFW_PRESS) {
        inputX -= 1.0f;
        inputZ += 1.0f;
    }
    if (glfwGetKey(window, bindings.keyRight) == GLFW_PRESS) {
        inputX += 1.0f;
        inputZ -= 1.0f;
    }

    const float len = std::sqrt(inputX * inputX + inputZ * inputZ);
    if (len > 0.0001f) {
        x_ += (inputX / len) * bindings.moveSpeed * dt;
        z_ += (inputZ / len) * bindings.moveSpeed * dt;
    }

    // Clamp to terrain bounds
    if (mapWidth > 0) {
        x_ = std::max(0.0f, std::min(static_cast<float>(mapWidth - 1), x_));
    }
    if (mapHeight > 0) {
        z_ = std::max(0.0f, std::min(static_cast<float>(mapHeight - 1), z_));
    }

    // Apply gravity against terrain height
    constexpr float gravity = -9.8f;
    velY_ += gravity * dt;
    y_ += velY_ * dt;

    float groundY;
    if (terrainMeshReady && terrainMesh) {
        groundY = terrainMesh->sampleHeight(x_, z_) + kPlayerHalfHeight;
    } else {
        groundY = SceneLoader::sampleTerrainHeight(heightMap, mapWidth, mapHeight, x_, z_) + kPlayerHalfHeight;
    }
    if (y_ < groundY) {
        y_ = groundY;
        if (velY_ < 0.0f) velY_ = 0.0f;
    }
}

void PlayerController::syncToInstances(std::vector<RenderInstance>& instances) const
{
    for (auto& instance : instances) {
        if (instance.isPlayer) {
            instance.position.x = x_;
            instance.position.y = y_;
            instance.position.z = z_;
        }
    }
}

} // namespace dash::vkexp
