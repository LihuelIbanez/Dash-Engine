#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

#include <vulkan/vulkan.h>

#include "rendering/mesh/MeshBuffers.h"
#include "rendering/platform/WindowContext.h"
#include "rendering/vulkan/DeviceContext.h"
#include "rendering/vulkan/FrameGraphLite.h"
#include "rendering/vulkan/SwapchainContext.h"
#include "game/physics/PhysicsWorld.h"
#include "game/physics/TransformProxy.h"
#include "input/InputBindings3D.h"

namespace dash::vkexp {

class Renderer {
public:
    struct RenderInstance {
        dash::physics::Vec3 position{};
        dash::physics::Vec3 scale{1.0f, 1.0f, 1.0f};
        dash::physics::Vec3 color{0.7f, 0.7f, 0.7f};
        bool isPlayer = false;
    };

    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(WindowContext& window);
    bool runSmoke(WindowContext& window, uint32_t targetFrames);
    void setScenePath(const std::string& scenePath);
    void setEditorStatePath(const std::string& statePath);
    void setEmbeddedPreview(bool enabled);
    void shutdown();

private:
    bool createInstance(const std::vector<const char*>& requiredExtensions);
    bool createDescriptors();
    bool createPipeline();
    bool createPerFrameUniformBuffers();
    bool updateCameraUbo(uint32_t imageIndex);
    void applyEditorStateIfNeeded(GLFWwindow* window);

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    DeviceContext deviceContext_;
    SwapchainContext swapchain_;
    MeshBuffers meshBuffers_;
    FrameGraphLite frameGraph_;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;
    std::vector<VkBuffer> uniformBuffers_;
    std::vector<VkDeviceMemory> uniformMemories_;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    bool initialized_ = false;

    dash::physics::PhysicsWorld physicsWorld_;
    dash::physics::TransformProxy transformProxy_;
    dash::physics::Transform3 cubeTransform_{};
    std::vector<RenderInstance> sceneInstances_;
    std::vector<RenderInstance> terrainInstances_;
    std::vector<float> terrainHeightMap_;
    int terrainMapWidth_ = 0;
    int terrainMapHeight_ = 0;
    int floorBodyId_ = -1;
    int cubeBodyId_ = -1;
    float fixedAccumulator_ = 0.0f;

    float cameraX_ = 0.0f;
    float cameraY_ = 1.5f;
    float cameraZ_ = 2.2f;
    float yawDegrees_ = -90.0f;
    float pitchDegrees_ = -20.0f;
    bool pendingAutoFocus_ = false;
    bool hadLookFrame_ = false;
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;

    // Player position (from scene)
    float playerX_ = 32.0f;
    float playerY_ = 1.0f;
    float playerZ_ = 32.0f;
    float playerVelY_ = 0.0f;
    float followDistance_ = 8.0f;
    float followHeight_ = 2.5f;

    bool embeddedPreview_ = false;
    std::string scenePath_;
    std::string editorStatePath_;
    std::chrono::steady_clock::time_point lastEditorStateRead_{};
    bool hasExternalSelection_ = false;
    bool loggedEmbeddedDocking_ = false;

    // Track editor state changes to avoid overwriting WASD input every frame
    float lastEditorTargetX_ = 0.0f;
    float lastEditorTargetZ_ = 0.0f;
    float lastEditorZoom_ = 1.0f;
    float lastEditorYaw_ = -90.0f;
    float lastEditorPitch_ = 0.0f;
    float lastEditorFollowDistance_ = 8.0f;
    float lastEditorFollowHeight_ = 2.5f;

    bool playerLoaded_ = false;
    InputBindings3D inputBindings_;
};

} // namespace dash::vkexp
