#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <vulkan/vulkan.h>

namespace dash::vkexp {

// Tunables for the screen-space occlusion. Authored per viewport; the runtime
// keeps the defaults unless a caller overrides them.
struct SsaoParams {
    bool  enabled = true;
    float radius = 0.55f;     // world units sampled around the shaded point
    float intensity = 0.90f;  // 0 = off, 1 = the raw hemisphere estimate
    float bias = 0.025f;      // view-space depth slack, kills self-occlusion acne
    float power = 1.35f;      // contrast curve applied to the final factor
};

// ─────────────────────────────────────────────────────────────────────────────
// SsaoPass — depth prepass + SSAO + separable bilateral blur, all at half the
// scene resolution, feeding one R8 image the forward shaders sample to darken
// their ambient term.
//
// The renderer is forward and the scene depth is only written during the shading
// pass itself, so this owns a depth attachment of its own and re-records the
// opaque geometry into it before the frame's main pass. Its depth render pass is
// laid out exactly like ShadowMap's, which is what lets the same
// PipelineBuilder::createShadowDepthPipeline shaders be reused for the prepass.
//
// Everything is optional: when init() or createResources() fails the caller
// binds a 1x1 white texel instead and the shaders resolve to "no occlusion".
// ─────────────────────────────────────────────────────────────────────────────
class SsaoPass {
public:
    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
    static constexpr VkFormat kAoFormat = VK_FORMAT_R8_UNORM;

    SsaoPass() = default;
    ~SsaoPass() = default;

    SsaoPass(const SsaoPass&) = delete;
    SsaoPass& operator=(const SsaoPass&) = delete;

    // Render passes, sampler, descriptor plumbing and the two fullscreen
    // pipelines. All size independent, so it survives every resize.
    bool init(VkDevice device, const std::string& shaderDir);

    // Images, framebuffers and descriptor writes. `sceneWidth/Height` are the
    // full scene resolution; the pass itself runs at half. Call again after
    // destroyResources() to resize.
    bool createResources(VkPhysicalDevice physicalDevice, VkDevice device,
                         uint32_t sceneWidth, uint32_t sceneHeight);

    // Depth prepass pipelines. Separate from createResources() because they need
    // the scene and bone set layouts, which are built later, and because their
    // viewport is baked in: destroyResources() drops them too.
    bool createPipelines(VkDevice device, const std::string& shaderDir,
                         VkDescriptorSetLayout sceneSetLayout,
                         VkDescriptorSetLayout boneSetLayout);

    void destroyResources(VkDevice device);

    void shutdown(VkDevice device);

    // Depth prepass. Clears to 1.0 and leaves the attachment in
    // DEPTH_STENCIL_READ_ONLY_OPTIMAL, so it is entered every frame even with
    // nothing to draw.
    void beginDepthPass(VkCommandBuffer cmd) const;
    void endDepthPass(VkCommandBuffer cmd) const;

    // SSAO + the two blur passes. Must be recorded after endDepthPass() and
    // before the scene pass that samples aoView().
    void recordResolve(VkCommandBuffer cmd, const SsaoParams& params,
                       float projXX, float projYY, float zNear, float zFar) const;

    bool valid() const { return aoImage_ != VK_NULL_HANDLE; }
    bool hasPipelines() const { return depthPipeline_ != VK_NULL_HANDLE; }

    VkImageView aoView() const { return aoView_; }
    VkSampler   sampler() const { return sampler_; }
    VkExtent2D  extent() const { return extent_; }

    // Depth prepass pipelines, mirroring the ShadowMap accessors so the caller
    // can feed both from the same SceneDrawParams shape.
    VkPipeline       depthPipeline() const { return depthPipeline_; }
    VkPipelineLayout depthPipelineLayout() const { return depthPipelineLayout_; }
    VkPipeline       skinnedPipeline() const { return skinnedPipeline_; }
    VkPipelineLayout skinnedPipelineLayout() const { return skinnedPipelineLayout_; }
    VkPipeline       terrainPipeline() const { return terrainPipeline_; }
    VkPipelineLayout terrainPipelineLayout() const { return terrainPipelineLayout_; }
    VkPipeline       billboardPipeline() const { return billboardPipeline_; }
    VkPipelineLayout billboardPipelineLayout() const { return billboardPipelineLayout_; }

private:
    void destroyDepthPipelines(VkDevice device);
    void drawFullscreen(VkCommandBuffer cmd, VkFramebuffer target,
                        VkPipeline pipeline, VkPipelineLayout layout,
                        VkDescriptorSet set, const float (&pushConstants)[16]) const;

    VkRenderPass depthRenderPass_ = VK_NULL_HANDLE;
    VkRenderPass aoRenderPass_ = VK_NULL_HANDLE;
    VkSampler    sampler_ = VK_NULL_HANDLE;
    // D32_SFLOAT has no guaranteed linear filtering support, and every depth tap
    // is texel aligned anyway.
    VkSampler    depthSampler_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      descriptorPool_ = VK_NULL_HANDLE;
    // 0 = SSAO (depth only), 1 = horizontal blur, 2 = vertical blur.
    std::array<VkDescriptorSet, 3> sets_{};

    VkPipelineLayout ssaoPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       ssaoPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout blurPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       blurPipeline_ = VK_NULL_HANDLE;

    VkImage        depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView    depthView_ = VK_NULL_HANDLE;
    VkFramebuffer  depthFramebuffer_ = VK_NULL_HANDLE;

    // Raw SSAO, the horizontal ping, and the blurred result the scene samples.
    VkImage        rawImage_ = VK_NULL_HANDLE;
    VkDeviceMemory rawMemory_ = VK_NULL_HANDLE;
    VkImageView    rawView_ = VK_NULL_HANDLE;
    VkFramebuffer  rawFramebuffer_ = VK_NULL_HANDLE;
    VkImage        pingImage_ = VK_NULL_HANDLE;
    VkDeviceMemory pingMemory_ = VK_NULL_HANDLE;
    VkImageView    pingView_ = VK_NULL_HANDLE;
    VkFramebuffer  pingFramebuffer_ = VK_NULL_HANDLE;
    VkImage        aoImage_ = VK_NULL_HANDLE;
    VkDeviceMemory aoMemory_ = VK_NULL_HANDLE;
    VkImageView    aoView_ = VK_NULL_HANDLE;
    VkFramebuffer  aoFramebuffer_ = VK_NULL_HANDLE;

    VkExtent2D extent_{};

    VkPipelineLayout depthPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       depthPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout skinnedPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       skinnedPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout terrainPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       terrainPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout billboardPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       billboardPipeline_ = VK_NULL_HANDLE;
};

} // namespace dash::vkexp
