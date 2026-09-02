#pragma once

#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include "imgui.h"
#include "assets/cache/AssetCache3D.h"
#include "rendering/animation/BonePalette.h"
#include "rendering/vulkan/ColorGrading.h"
#include "rendering/vulkan/DeviceContext.h"
#include "rendering/vulkan/SwapchainContext.h"
#include "rendering/vulkan/FrameGraphLite.h"
#include "rendering/vulkan/HdrTarget.h"
#include "rendering/vulkan/ParticleRenderer.h"
#include "rendering/vulkan/SceneRenderer.h"
#include "rendering/vulkan/ShadowMap.h"
#include "rendering/vulkan/SsaoPass.h"
#include "rendering/mesh/MeshBuffers.h"
#include "rendering/textures/TerrainTextureArray.h"
#include "world/TerrainMesh.h"
#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// EditorVkContext – Vulkan lifecycle + offscreen viewport for the editor
// ─────────────────────────────────────────────────────────────────────────────
class EditorVkContext {
public:
    bool init(SDL_Window* window);
    void shutdown();

    // ── Per-frame lifecycle (wraps swapchain acquire / present) ──────────────
    bool beginFrame();            // acquire swapchain image
    VkCommandBuffer currentCmd(); // command buffer for this frame
    void endFrame();              // submit + present

    // ── Offscreen viewport ──────────────────────────────────────────────────
    // Reallocates the viewport-sized targets when the panel changed size. Called
    // by beginViewportRender(), and separately by callers that need the targets
    // final before recording the depth passes that precede it.
    void ensureViewportSize(uint32_t width, uint32_t height);
    void beginViewportRender(uint32_t width, uint32_t height);
    void endViewportRender();
    ImTextureID viewportTexture() const;

    // ── Terrain mesh management ─────────────────────────────────────────────
    void updateTerrainMesh(TerrainMesh& terrain);

    // ── Camera UBO ──────────────────────────────────────────────────────────
    void updateCamera(const float viewProj[16]);

    // ── Scene light UBO (set 0, binding 2) ──────────────────────────────────
    // Only the camera block plus `count` lights are uploaded; the rest is never
    // read by the shader.
    void updateSceneLights(const dash::vkexp::SceneLightsUbo& ubo, int count);

    // ── Directional shadow cascades ─────────────────────────────────────────
    // Mirrors the runtime renderer so the viewport shows the same shadows the
    // game will. False means the depth target never came up, in which case the
    // scene layout has no binding 3 and the "_lit" fragment variants are bound.
    bool shadowsEnabled() const { return shadowMap_.valid(); }

    // Recomputes the per-cascade light matrices for this camera and light.
    // `lightIndex` < 0 leaves the block zeroed, which disables the lookup.
    void updateShadowCascades(const dash::vkexp::Vec3& camPos,
                              const dash::vkexp::Vec3& forward,
                              const dash::vkexp::Vec3& right,
                              const dash::vkexp::Vec3& up,
                              float fovYRadians, float aspect,
                              const dash::vkexp::Vec3& lightDir,
                              int lightIndex);

    // Copies the cascade block into the UBO the caller is about to upload.
    void fillShadowUbo(dash::vkexp::SceneLightsUbo& ubo) const;

    // One depth pass per cascade over the same casters the viewport draws.
    // Must be recorded before beginViewportRender(): render passes cannot nest.
    void recordShadowPass(const std::vector<dash::vkexp::RenderInstance>& instances,
                          const std::vector<dash::vkexp::InstanceResources>& resources);

    // ── Screen-space ambient occlusion ──────────────────────────────────────
    // Depth prepass + resolve, same as the runtime. Also has to be recorded
    // before beginViewportRender().
    void recordSsaoPass(const std::vector<dash::vkexp::RenderInstance>& instances,
                        const std::vector<dash::vkexp::InstanceResources>& resources,
                        const dash::vkexp::Mat4& viewProj, float aspect);

    dash::vkexp::SsaoParams& ssaoParams() { return ssaoParams_; }

    // ── Pipeline access ─────────────────────────────────────────────────────
    VkPipeline          terrainPipeline()       const { return terrainPipeline_; }
    VkPipelineLayout    terrainPipelineLayout() const { return terrainPipelineLayout_; }
    VkPipeline          waterPipeline()         const { return waterPipeline_; }
    VkPipelineLayout    waterPipelineLayout()   const { return waterPipelineLayout_; }
    VkPipeline          basicPipeline()         const { return basicPipeline_; }
    VkPipelineLayout    basicPipelineLayout()   const { return basicPipelineLayout_; }
    // VK_NULL_HANDLE when the *_lit shaders are missing; callers must fall back.
    VkPipeline          basicLitPipeline()       const { return basicLitPipeline_; }
    VkPipelineLayout    basicLitPipelineLayout() const { return basicLitPipelineLayout_; }
    // Same shaders as basicPipeline() but front-face culled, so drawing an
    // entity's own mesh enlarged through this pipeline only rasterizes the
    // silhouette rim (the front-facing bulk depth-fails against the mesh's
    // own regular draw) - the GPU half of the Blender-style selection outline.
    VkPipeline          outlinePipeline()       const { return outlinePipeline_; }
    VkPipelineLayout    outlinePipelineLayout() const { return outlinePipelineLayout_; }
    VkPipeline          billboardPipeline()       const { return billboardPipeline_; }
    VkPipelineLayout    billboardPipelineLayout() const { return billboardPipelineLayout_; }
    VkDescriptorSet     sceneDescriptorSet()    const { return sceneDescSet_; }

    // ── Skinning ────────────────────────────────────────────────────────────
    // VK_NULL_HANDLE when the bone palette or the skinned shaders are missing;
    // drawSceneInstances then falls back to drawing skinned meshes in bind pose.
    VkPipeline          skinnedPipeline()       const { return skinnedPipeline_; }
    VkPipelineLayout    skinnedPipelineLayout() const { return skinnedPipelineLayout_; }
    VkDescriptorSet     boneDescriptorSet()     const { return boneSet_; }
    dash::anim::BonePalette& bonePalette() { return bonePalette_; }
    // Swapchain image being recorded: which bone palette region this frame owns.
    // Only meaningful between beginFrame() and endFrame().
    uint32_t currentFrameIndex() const { return currentImageIndex_; }

    // ── Mesh buffer access ──────────────────────────────────────────────────
    const dash::vkexp::MeshBuffers& terrainMesh()  const { return terrainMeshBuf_; }
    const dash::vkexp::MeshBuffers& waterMesh()    const { return waterMeshBuf_; }
    const dash::vkexp::MeshBuffers& cubeMesh()     const { return cubeMeshBuf_; }

    // Resolves RenderComponent::mesh to a loaded mesh, caching hits and misses.
    // Returns nullptr for the builtin cube or when the model cannot be loaded.
    const dash::vkexp::MeshBuffers* resolveMesh(const std::string& meshId);

    // Absolute path a mesh id maps to, or empty. Animation wiring derives the
    // .dashskel/.dashanim siblings from it.
    std::string resolveModelPath(const std::string& meshId) const;

    // Scene pass the viewport pipelines are built against (HDR, not the LDR
    // image ImGui ends up sampling).
    VkRenderPass viewportRenderPass() const { return hdr_.renderPass(); }

    // ── Combat VFX (blood/impact/death particles) ──────────────────────────
    // Batches are built by the caller's CPU-side dash::vfx::ParticleSystem;
    // this just uploads and draws them, same as Renderer::particles_.record().
    // No-op (and cheap) when particles_ failed to come up.
    void recordParticles(const dash::vkexp::Mat4& viewProj,
                        const dash::vkexp::Vec3& camRight, const dash::vkexp::Vec3& camUp,
                        const std::vector<dash::vfx::ParticleInstance>& alphaBatch,
                        const std::vector<dash::vfx::ParticleInstance>& additiveBatch);

    // The actual size of the offscreen render target right now, which can lag
    // a frame or more behind the panel's own size: ensureViewportSize() only
    // reallocates past a hysteresis threshold. Anything building a projection
    // or NDC<->pixel mapping for the 3D view must use these, not the panel's
    // raw size, or its aspect ratio drifts from what Vulkan actually renders.
    uint32_t viewportWidth() const { return vpWidth_; }
    uint32_t viewportHeight() const { return vpHeight_; }

private:
    bool createInstance(SDL_Window* window);
    bool createOffscreenTarget(uint32_t w, uint32_t h);
    void destroyOffscreenTarget();
    bool createSceneDescriptors();
    bool createTerrainTextureArray();
    bool createPipelines();

    // Bone palette ring plus its dynamic-offset descriptor (set 1), shared by
    // every skinned draw in a frame. Mirrors Renderer::createBoneResources.
    bool createBoneResources();
    void destroyBoneResources();

    // Vulkan core
    VkInstance                       instance_    = VK_NULL_HANDLE;
    VkSurfaceKHR                     surface_     = VK_NULL_HANDLE;
    dash::vkexp::DeviceContext       deviceCtx_;
    dash::vkexp::SwapchainContext    swapchain_;
    dash::vkexp::FrameGraphLite      frameGraph_;

    // Offscreen viewport render target
    VkRenderPass   vpRenderPass_    = VK_NULL_HANDLE;
    VkImage        vpColorImage_    = VK_NULL_HANDLE;
    VkDeviceMemory vpColorMemory_   = VK_NULL_HANDLE;
    VkImageView    vpColorView_     = VK_NULL_HANDLE;
    VkFramebuffer  vpFramebuffer_   = VK_NULL_HANDLE;
    VkDescriptorSet vpImGuiDesc_    = VK_NULL_HANDLE;
    VkSampler      vpSampler_       = VK_NULL_HANDLE;
    uint32_t       vpWidth_  = 0;
    uint32_t       vpHeight_ = 0;

    // Scene target plus the tonemap that resolves it into vpColorImage_. ImGui
    // samples that image raw onto a _UNORM swapchain, so the resolve is the one
    // that has to encode sRGB by hand.
    dash::vkexp::HdrTarget      hdr_;    dash::vkexp::GradingParams  grading_;
    dash::vkexp::ParticleRenderer particles_;

    // Scene descriptor (UBO binding 0)
    VkDescriptorSetLayout sceneDescLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      sceneDescPool_   = VK_NULL_HANDLE;
    VkDescriptorSet       sceneDescSet_    = VK_NULL_HANDLE;
    VkImage          dummyTexImage_   = VK_NULL_HANDLE;
    VkDeviceMemory   dummyTexMemory_  = VK_NULL_HANDLE;
    VkImageView      dummyTexView_    = VK_NULL_HANDLE;
    VkBuffer              uboBuffer_       = VK_NULL_HANDLE;
    VkDeviceMemory        uboMemory_       = VK_NULL_HANDLE;
    void*                 uboMapped_       = nullptr; // persistently mapped
    VkBuffer              lightUboBuffer_  = VK_NULL_HANDLE;
    VkDeviceMemory        lightUboMemory_  = VK_NULL_HANDLE;
    void*                 lightUboMapped_  = nullptr; // persistently mapped

    // Pipelines
    VkPipelineLayout terrainPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       terrainPipeline_       = VK_NULL_HANDLE;
    VkPipelineLayout waterPipelineLayout_   = VK_NULL_HANDLE;
    VkPipeline       waterPipeline_         = VK_NULL_HANDLE;
    VkPipelineLayout basicPipelineLayout_   = VK_NULL_HANDLE;
    VkPipeline       basicPipeline_         = VK_NULL_HANDLE;
    VkPipelineLayout basicLitPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       basicLitPipeline_       = VK_NULL_HANDLE;
    VkPipelineLayout outlinePipelineLayout_  = VK_NULL_HANDLE;
    VkPipeline       outlinePipeline_        = VK_NULL_HANDLE;
    VkPipelineLayout billboardPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       billboardPipeline_       = VK_NULL_HANDLE;
    VkPipelineLayout skinnedPipelineLayout_   = VK_NULL_HANDLE;
    VkPipeline       skinnedPipeline_         = VK_NULL_HANDLE;

    // Bone palette (set 1, one dynamic-offset UBO)
    VkDescriptorSetLayout   boneSetLayout_  = VK_NULL_HANDLE;
    VkDescriptorPool        boneDescPool_   = VK_NULL_HANDLE;
    VkDescriptorSet         boneSet_        = VK_NULL_HANDLE;
    VkBuffer                boneBuffer_     = VK_NULL_HANDLE;
    VkDeviceMemory          boneMemory_     = VK_NULL_HANDLE;
    dash::anim::BonePalette bonePalette_;

    dash::vkexp::AssetCache3D meshCache_;

    // Cascaded shadow map, sized and tuned exactly like the runtime one.
    dash::vkexp::ShadowMap shadowMap_;
    dash::vkexp::Mat4      shadowMatrices_[dash::vkexp::kShadowCascades]{};
    float                  shadowSplits_[4]{};
    float                  shadowTexels_[4]{};
    float                  shadowDepthBias_[4]{};
    float                  shadowParams_[4]{};
    dash::vkexp::Vec3      shadowLightDir_{0.0f, -1.0f, 0.0f};
    int                    shadowLightIndex_ = -1;
    bool                   shadowLogged_ = false;

    dash::vkexp::SsaoPass   ssao_;
    dash::vkexp::SsaoParams ssaoParams_;

    // Terrain texture arrays (shared with the runtime renderer)
    dash::vkexp::TerrainTextureSet terrainTextures_;

    // Mesh buffers
    dash::vkexp::MeshBuffers terrainMeshBuf_;
    dash::vkexp::MeshBuffers waterMeshBuf_;
    dash::vkexp::MeshBuffers cubeMeshBuf_;

    uint32_t currentImageIndex_ = 0;
    bool     frameInFlight_     = false;
};
