#pragma once

#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include "imgui.h"
#include "rendering/vulkan/DeviceContext.h"
#include "rendering/vulkan/SwapchainContext.h"
#include "rendering/vulkan/FrameGraphLite.h"
#include "rendering/mesh/MeshBuffers.h"
#include "world/TerrainMesh.h"
#include <cstdint>

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
    void beginViewportRender(uint32_t width, uint32_t height);
    void endViewportRender();
    ImTextureID viewportTexture() const;

    // ── Terrain mesh management ─────────────────────────────────────────────
    void updateTerrainMesh(TerrainMesh& terrain);

    // ── Camera UBO ──────────────────────────────────────────────────────────
    void updateCamera(const float viewProj[16]);

    // ── Pipeline access ─────────────────────────────────────────────────────
    VkPipeline          terrainPipeline()       const { return terrainPipeline_; }
    VkPipelineLayout    terrainPipelineLayout() const { return terrainPipelineLayout_; }
    VkPipeline          waterPipeline()         const { return waterPipeline_; }
    VkPipelineLayout    waterPipelineLayout()   const { return waterPipelineLayout_; }
    VkPipeline          basicPipeline()         const { return basicPipeline_; }
    VkPipelineLayout    basicPipelineLayout()   const { return basicPipelineLayout_; }
    VkDescriptorSet     sceneDescriptorSet()    const { return sceneDescSet_; }

    // ── Mesh buffer access ──────────────────────────────────────────────────
    const dash::vkexp::MeshBuffers& terrainMesh()  const { return terrainMeshBuf_; }
    const dash::vkexp::MeshBuffers& waterMesh()    const { return waterMeshBuf_; }
    const dash::vkexp::MeshBuffers& cubeMesh()     const { return cubeMeshBuf_; }
    const dash::vkexp::MeshBuffers& wolfMesh()     const { return wolfMeshBuf_; }

    VkRenderPass viewportRenderPass() const { return vpRenderPass_; }

private:
    bool createInstance(SDL_Window* window);
    bool createOffscreenTarget(uint32_t w, uint32_t h);
    void destroyOffscreenTarget();
    bool createSceneDescriptors();
    bool createTerrainTextureArray();
    bool createPipelines();

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
    VkImage        vpDepthImage_    = VK_NULL_HANDLE;
    VkDeviceMemory vpDepthMemory_   = VK_NULL_HANDLE;
    VkImageView    vpDepthView_     = VK_NULL_HANDLE;
    VkFramebuffer  vpFramebuffer_   = VK_NULL_HANDLE;
    VkDescriptorSet vpImGuiDesc_    = VK_NULL_HANDLE;
    VkSampler      vpSampler_       = VK_NULL_HANDLE;
    uint32_t       vpWidth_  = 0;
    uint32_t       vpHeight_ = 0;

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

    // Pipelines
    VkPipelineLayout terrainPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       terrainPipeline_       = VK_NULL_HANDLE;
    VkPipelineLayout waterPipelineLayout_   = VK_NULL_HANDLE;
    VkPipeline       waterPipeline_         = VK_NULL_HANDLE;
    VkPipelineLayout basicPipelineLayout_   = VK_NULL_HANDLE;
    VkPipeline       basicPipeline_         = VK_NULL_HANDLE;

    // Terrain texture array
    VkImage        terrainTexArrayImage_  = VK_NULL_HANDLE;
    VkDeviceMemory terrainTexArrayMemory_ = VK_NULL_HANDLE;
    VkImageView    terrainTexArrayView_   = VK_NULL_HANDLE;
    VkSampler      terrainTexSampler_     = VK_NULL_HANDLE;

    // Mesh buffers
    dash::vkexp::MeshBuffers terrainMeshBuf_;
    dash::vkexp::MeshBuffers waterMeshBuf_;
    dash::vkexp::MeshBuffers cubeMeshBuf_;
    dash::vkexp::MeshBuffers wolfMeshBuf_;

    uint32_t currentImageIndex_ = 0;
    bool     frameInFlight_     = false;
};
