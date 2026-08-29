#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <vulkan/vulkan.h>

#include "rendering/vulkan/ShadowMath.h"

namespace dash::vkexp {

// ─────────────────────────────────────────────────────────────────────────────
// ShadowMap — depth-only render target for the single directional light that
// declares castsShadows. Owns a kShadowCascades-layer depth array, its render
// pass, one framebuffer per layer, a comparison sampler and the two depth-only
// pipelines (static + skinned).
//
// Everything is optional: when init() fails the renderer keeps the shader
// variants without the shadow binding and the frame looks exactly as before.
// ─────────────────────────────────────────────────────────────────────────────
class ShadowMap {
public:
    // 2048x2048 D32 per cascade = 16 MiB each. The near cascade covers a few
    // metres, so that is around 1.5 cm per texel where the character stands —
    // fine enough that the 3x3 PCF kernel reads as a soft edge rather than a
    // staircase, and three of them still fit far from the low-end budget.
    static constexpr uint32_t kResolution = 2048;

    ShadowMap() = default;
    ~ShadowMap() = default;

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    // Image + render pass + framebuffers + sampler. Independent of the scene, so
    // it can run before the descriptor layouts are built.
    bool initTarget(VkPhysicalDevice physicalDevice, VkDevice device);

    // Depth-only pipelines. `boneSetLayout` may be VK_NULL_HANDLE, in which
    // case skinned casters fall back to their static (bind pose) silhouette.
    bool createPipelines(VkDevice device,
                         const std::string& shaderDir,
                         VkDescriptorSetLayout sceneSetLayout,
                         VkDescriptorSetLayout boneSetLayout);

    void shutdown(VkDevice device);

    // Clears the given cascade to 1.0 and leaves that layer in
    // DEPTH_STENCIL_READ_ONLY_OPTIMAL. Every cascade is entered each frame even
    // without casters, so the descriptor always points at layers in a layout the
    // fragment stage can sample.
    void beginPass(VkCommandBuffer cmd, int cascade) const;
    void endPass(VkCommandBuffer cmd) const;

    bool valid() const { return image_ != VK_NULL_HANDLE; }
    bool hasPipelines() const { return pipeline_ != VK_NULL_HANDLE; }

    VkImageView imageView() const { return arrayView_; }
    VkSampler   sampler() const { return sampler_; }

    VkPipeline       pipeline() const { return pipeline_; }
    VkPipelineLayout pipelineLayout() const { return pipelineLayout_; }
    VkPipeline       skinnedPipeline() const { return skinnedPipeline_; }
    VkPipelineLayout skinnedPipelineLayout() const { return skinnedPipelineLayout_; }
    // Same shader as pipeline(), only the vertex stride differs.
    VkPipeline       terrainPipeline() const { return terrainPipeline_; }
    VkPipelineLayout terrainPipelineLayout() const { return terrainPipelineLayout_; }
    VkPipeline       billboardPipeline() const { return billboardPipeline_; }
    VkPipelineLayout billboardPipelineLayout() const { return billboardPipelineLayout_; }

private:
    VkImage        image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    // Whole array, for sampling; the per-layer views exist only to be rendered into.
    VkImageView    arrayView_ = VK_NULL_HANDLE;
    std::array<VkImageView, kShadowCascades>   layerViews_{};
    std::array<VkFramebuffer, kShadowCascades> framebuffers_{};
    VkRenderPass   renderPass_ = VK_NULL_HANDLE;
    VkSampler      sampler_ = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout skinnedPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       skinnedPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout terrainPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       terrainPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout billboardPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       billboardPipeline_ = VK_NULL_HANDLE;
};

} // namespace dash::vkexp
