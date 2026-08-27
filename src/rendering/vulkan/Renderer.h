#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "rendering/mesh/MeshBuffers.h"
#include "rendering/textures/TextureLoader.h"
#include "assets/cache/AssetCache3D.h"
#include "rendering/platform/WindowContext.h"
#include "rendering/vulkan/DeviceContext.h"
#include "rendering/vulkan/EditorBridge.h"
#include "rendering/vulkan/FrameGraphLite.h"
#include "rendering/vulkan/SwapchainContext.h"
#include "rendering/vulkan/CameraController.h"
#include "rendering/vulkan/PlayerController.h"
#include "rendering/vulkan/RenderTypes.h"
#include "game/physics/PhysicsWorld.h"
#include "game/physics/TransformProxy.h"
#include "input/InputBindings3D.h"
#include "world/TerrainMesh.h"

namespace dash::vkexp {

class Renderer {
public:
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
    void recordDrawCommands(VkCommandBuffer cmd, uint32_t imageIndex);

    // Resolves RenderComponent::mesh for every scene instance, uploading and
    // caching referenced models. Falls back to the builtin cube when missing.
    void resolveSceneMeshes();
    const MeshBuffers* resolveMesh(const std::string& meshId);

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

    VkPipelineLayout texturedPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline texturedPipeline_ = VK_NULL_HANDLE;
    TextureResource defaultTexture_{};

    VkPipelineLayout terrainPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline terrainPipeline_ = VK_NULL_HANDLE;
    MeshBuffers terrainMeshBuffers_;

    VkPipelineLayout waterPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline waterPipeline_ = VK_NULL_HANDLE;
    MeshBuffers waterMeshBuffers_;

    bool initialized_ = false;

    AssetCache3D assetCache_;

    dash::physics::PhysicsWorld physicsWorld_;
    dash::physics::TransformProxy transformProxy_;
    dash::physics::Transform3 cubeTransform_{};
    std::vector<RenderInstance> sceneInstances_;
    // Resolved mesh per scene instance, aligned by index with sceneInstances_.
    std::vector<const MeshBuffers*> sceneInstanceMeshes_;
    std::vector<RenderInstance> terrainInstances_;
    std::vector<float> terrainHeightMap_;
    int terrainMapWidth_ = 0;
    int terrainMapHeight_ = 0;
    TerrainMesh terrainMesh_;
    bool terrainMeshReady_ = false;
    int floorBodyId_ = -1;
    int cubeBodyId_ = -1;
    float fixedAccumulator_ = 0.0f;

    CameraController camera_;
    PlayerController player_;
    EditorBridge editorBridge_;

    float elapsedSeconds_ = 0.0f;
    std::string scenePath_;
    InputBindings3D inputBindings_;
};

} // namespace dash::vkexp
