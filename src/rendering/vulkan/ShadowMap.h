#pragma once

#include <cstdint>
#include <string>

#include <vulkan/vulkan.h>

namespace dash::vkexp {

// ─────────────────────────────────────────────────────────────────────────────
// ShadowMap — depth-only render target for the single directional light that
// declares castsShadows. Owns its image, render pass, framebuffer, comparison
// sampler and the two depth-only pipelines (static + skinned).
//
// Everything is optional: when init() fails the renderer keeps the shader
// variants without the shadow binding and the frame looks exactly as before.
// ─────────────────────────────────────────────────────────────────────────────
class ShadowMap {
public:
    // 2048x2048 D32 = 16 MiB. Over the ~120-world-unit volume of the densest
    // shipped scene that is ~6 cm per texel, fine enough that the 3x3 PCF
    // kernel reads as a soft edge instead of a staircase, and small enough to
    // stay far from the 128 MiB budget of the low-end targets.
    static constexpr uint32_t kResolution = 2048;

    ShadowMap() = default;
    ~ShadowMap() = default;

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    // Image + render pass + framebuffer + sampler. Independent of the scene, so
    // it can run before the descriptor layouts are built.
    bool initTarget(VkPhysicalDevice physicalDevice, VkDevice device);

    // Depth-only pipelines. `boneSetLayout` may be VK_NULL_HANDLE, in which
    // case skinned casters fall back to their static (bind pose) silhouette.
    bool createPipelines(VkDevice device,
                         const std::string& shaderDir,
                         VkDescriptorSetLayout sceneSetLayout,
                         VkDescriptorSetLayout boneSetLayout);

    void shutdown(VkDevice device);

    // Clears to 1.0 and leaves the image in DEPTH_STENCIL_READ_ONLY_OPTIMAL.
    // Called every frame even without casters, so the descriptor always points
    // at an image in a layout the fragment stage can sample.
    void beginPass(VkCommandBuffer cmd) const;
    void endPass(VkCommandBuffer cmd) const;

    bool valid() const { return image_ != VK_NULL_HANDLE; }
    bool hasPipelines() const { return pipeline_ != VK_NULL_HANDLE; }

    VkImageView imageView() const { return imageView_; }
    VkSampler   sampler() const { return sampler_; }

    VkPipeline       pipeline() const { return pipeline_; }
    VkPipelineLayout pipelineLayout() const { return pipelineLayout_; }
    VkPipeline       skinnedPipeline() const { return skinnedPipeline_; }
    VkPipelineLayout skinnedPipelineLayout() const { return skinnedPipelineLayout_; }

private:
    VkImage        image_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkImageView    imageView_ = VK_NULL_HANDLE;
    VkRenderPass   renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer  framebuffer_ = VK_NULL_HANDLE;
    VkSampler      sampler_ = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout skinnedPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       skinnedPipeline_ = VK_NULL_HANDLE;
};

} // namespace dash::vkexp
