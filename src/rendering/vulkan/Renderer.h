#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "rendering/mesh/MeshBuffers.h"
#include "rendering/textures/TextureLoader.h"
#include "rendering/textures/TerrainTextureArray.h"
#include "rendering/animation/AnimationPlayer.h"
#include "rendering/animation/AnimationWiring.h"
#include "rendering/animation/BonePalette.h"
#include "assets/cache/AssetCache3D.h"
#include "assets/AssetDatabase.h"
#include "assets/MaterialAsset.h"
#include "rendering/platform/WindowContext.h"
#include "rendering/vulkan/ColorGrading.h"
#include "rendering/vulkan/DeviceContext.h"
#include "rendering/vulkan/EditorBridge.h"
#include "rendering/vulkan/FrameGraphLite.h"
#include "rendering/vulkan/HdrTarget.h"
#include "rendering/vulkan/SwapchainContext.h"
#include "rendering/vulkan/CameraController.h"
#include "rendering/vulkan/PlayerController.h"
#include "rendering/vulkan/RenderTypes.h"
#include "rendering/vulkan/SceneRenderer.h"
#include "rendering/vulkan/ShadowMap.h"
#include "rendering/vulkan/ShadowMath.h"
#include "game/physics/PhysicsWorld.h"
#include "game/physics/TransformProxy.h"
#include "events/EventDispatcher.h"
#include "events/GameEvents.h"
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
    bool updateSceneLightsUbo(uint32_t imageIndex);
    void recordDrawCommands(VkCommandBuffer cmd, uint32_t imageIndex);

    // Picks the first directional light with castsShadows and fixes the tuning
    // that does not depend on the camera. Runs once, after the scene is loaded.
    void setupShadowLight();
    // Refits every cascade to its slice of the camera frustum and snaps each to
    // its own texel grid, so shadows follow the camera without the edges crawling.
    void updateShadowMatrix();
    // Depth-only pass, one per cascade, recorded before the main render pass of
    // the same frame.
    void recordShadowPass(VkCommandBuffer cmd, uint32_t imageIndex,
                          const std::vector<InstanceResources>& res);

    // Resolves RenderComponent::mesh for every scene instance, uploading and
    // caching referenced models. Falls back to the builtin cube when missing.
    void resolveSceneMeshes();
    const MeshBuffers* resolveMesh(const std::string& meshId);
    // Mesh id -> file on disk, empty when nothing matches the search paths.
    std::string resolveModelPath(const std::string& meshId) const;

    // Creates one AnimationPlayer per instance with an AnimationComponent.
    // Must run after resolveSceneMeshes(), which reorders sceneInstances_.
    void buildSceneAnimators();

    // Resolves RenderComponent::material, allocating one descriptor set per
    // material per swapchain image so textures can be bound per draw.
    // Registers a physics body per entity carrying a PhysicsComponent.
    void spawnSceneryPhysicsBodies(const LoadedScene& scene);
    // Drops every instance onto the terrain surface, treating its authored Y as
    // a height above the ground rather than an absolute one.
    void snapInstancesToTerrain();
    // Copies simulated body positions back into their render instances.
    void syncPhysicsToInstances();

    bool resolveSceneMaterials();
    void destroySceneMaterials();

    // Asset database used to turn a material GUID into its source path. Loaded
    // on first use so scenes that reference materials by path never read it.
    const AssetDatabase* materialDb();

    // Bone palette ring plus its dynamic-offset descriptor, shared by every
    // skinned draw in a frame.
    bool createBoneResources();
    void destroyBoneResources();
    // Advances every animated instance and fills its palette slot.
    void updateSkinnedInstances(float dt, std::vector<InstanceResources>& res);

    // Prints min/avg/p95/max for the samples collected during runSmoke().
    void reportFrameStats(uint32_t renderedFrames) const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    DeviceContext deviceContext_;
    SwapchainContext swapchain_;
    MeshBuffers meshBuffers_;
    FrameGraphLite frameGraph_;

    // Scene target: everything is shaded here in linear HDR, then resolved to
    // the swapchain through ACES + grading.
    HdrTarget hdr_;
    GradingParams grading_;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;
    std::vector<VkBuffer> uniformBuffers_;
    std::vector<VkDeviceMemory> uniformMemories_;

    // Scene lights (set 0, binding 2), one buffer per swapchain image.
    std::vector<VkBuffer> lightBuffers_;
    std::vector<VkDeviceMemory> lightMemories_;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    VkPipelineLayout texturedPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline texturedPipeline_ = VK_NULL_HANDLE;
    TextureResource defaultTexture_{};

    VkPipelineLayout terrainPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline terrainPipeline_ = VK_NULL_HANDLE;
    TerrainTextureSet terrainTextures_{};
    MeshBuffers terrainMeshBuffers_;

    VkPipelineLayout waterPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline waterPipeline_ = VK_NULL_HANDLE;
    MeshBuffers waterMeshBuffers_;

    VkPipelineLayout billboardPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline billboardPipeline_ = VK_NULL_HANDLE;

    VkPipelineLayout skinnedPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline skinnedPipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout boneSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool boneDescriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet boneSet_ = VK_NULL_HANDLE;
    VkBuffer boneBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory boneMemory_ = VK_NULL_HANDLE;
    dash::anim::BonePalette bonePalette_;
    // One player per scene instance carrying an AnimationComponent.
    std::unordered_map<size_t, dash::anim::AnimationPlayer> animators_;
    // Skeleton + clips shared by every instance of the same model.
    dash::anim::AnimationSetCache animationSets_;

    // Directional shadow map. `shadowLightIndex_` < 0 means no light asked for
    // shadows, and every shader then takes the pre-shadow code path.
    ShadowMap shadowMap_;
    int  shadowLightIndex_ = -1;
    Mat4 shadowMatrices_[kShadowCascades]{};
    float shadowSplits_[4]{};
    float shadowTexels_[4]{};
    float shadowDepthBias_[4]{};
    float shadowParams_[4]{};

    bool initialized_ = false;

    AssetCache3D assetCache_;

    dash::physics::PhysicsWorld physicsWorld_;
    dash::physics::TransformProxy transformProxy_;
    dash::physics::Transform3 cubeTransform_{};
    std::vector<RenderInstance> sceneInstances_;
    // Resolved mesh per scene instance, aligned by index with sceneInstances_.
    std::vector<const MeshBuffers*> sceneInstanceMeshes_;
    std::vector<SceneLight> sceneLights_;

    struct MaterialGpu {
        MaterialAsset asset;
        TextureResource texture{};
        bool ownsTexture = false;
        std::vector<VkDescriptorSet> sets;  // one per swapchain image
    };
    std::vector<MaterialGpu> materials_;
    // Index into materials_ per scene instance; -1 = default descriptor set.
    std::vector<int> sceneInstanceMaterials_;
    VkDescriptorPool materialDescriptorPool_ = VK_NULL_HANDLE;

    AssetDatabase assetDb_;
    bool assetDbLoadAttempted_ = false;
    bool assetDbLoaded_ = false;

    std::vector<RenderInstance> terrainInstances_;
    std::vector<float> terrainHeightMap_;
    int terrainMapWidth_ = 0;
    int terrainMapHeight_ = 0;
    TerrainMesh terrainMesh_;
    bool terrainMeshReady_ = false;
    int floorBodyId_ = -1;
    int cubeBodyId_ = -1;
    float fixedAccumulator_ = 0.0f;

    // Physics bodies spawned from PhysicsComponent, keyed by body id.
    std::unordered_map<int, uint64_t> bodyToEntity_;
    EventDispatcher events_;

    // Frustum culling counters from the most recent recorded frame.
    uint32_t lastDrawnInstances_ = 0;
    uint32_t lastCulledInstances_ = 0;

    // Per-frame timing samples collected during runSmoke(). recordMs_ isolates
    // CPU command recording, which wall-clock frame time hides under FIFO vsync.
    std::vector<float> frameMs_;
    std::vector<float> recordMs_;

    CameraController camera_;
    PlayerController player_;
    EditorBridge editorBridge_;

    float elapsedSeconds_ = 0.0f;
    // Last frame delta, applied to the animators while recording draw commands.
    float frameDeltaSeconds_ = 0.0f;
    std::string scenePath_;
    InputBindings3D inputBindings_;
};

} // namespace dash::vkexp
