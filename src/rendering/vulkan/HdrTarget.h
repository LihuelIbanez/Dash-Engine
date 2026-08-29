#pragma once

#include <cstdint>
#include <string>

#include <vulkan/vulkan.h>

#include "rendering/vulkan/ColorGrading.h"

namespace dash::vkexp {

// ─────────────────────────────────────────────────────────────────────────────
// HdrTarget — the scene render target plus the fullscreen tonemap that resolves
// it. The scene is shaded into an R16G16B16A16_SFLOAT attachment so values keep
// going past 1.0, and a second pass reads that back and writes the display
// image through ACES + grading.
//
// The runtime resolves into its _SRGB swapchain (hardware encodes, so the pass
// writes linear); the editor resolves into a _UNORM image ImGui samples raw, so
// there the pass encodes sRGB itself. Same shader, one push constant apart.
// ─────────────────────────────────────────────────────────────────────────────
class HdrTarget {
public:
    HdrTarget() = default;
    ~HdrTarget() = default;

    HdrTarget(const HdrTarget&) = delete;
    HdrTarget& operator=(const HdrTarget&) = delete;

    static constexpr VkFormat kColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

    // Render pass, sampler and the descriptor the tonemap pass reads. Size
    // independent, so it survives every resize.
    bool init(VkDevice device);

    // Colour + depth images, their views and the framebuffer. Call again after
    // destroyResources() to resize.
    bool createResources(VkPhysicalDevice physicalDevice, VkDevice device,
                         uint32_t width, uint32_t height);
    void destroyResources(VkDevice device);

    // `outputRenderPass` is where the resolved image lands: the swapchain pass
    // for the runtime, the viewport resolve pass for the editor.
    bool createPipeline(VkDevice device, VkRenderPass outputRenderPass,
                        const std::string& shaderDir);

    void shutdown(VkDevice device);

    // Scene pass. The render pass leaves the colour attachment in
    // SHADER_READ_ONLY_OPTIMAL, so drawTonemap() can sample it right after.
    void beginPass(VkCommandBuffer cmd, const float clearColor[4]) const;
    void endPass(VkCommandBuffer cmd) const;

    // Must be recorded inside `outputRenderPass`. Sets its own viewport and
    // scissor, so the pipeline is not tied to one target size.
    void drawTonemap(VkCommandBuffer cmd, const GradingParams& grading,
                     bool encodeSrgb) const;

    bool valid() const { return framebuffer_ != VK_NULL_HANDLE && pipeline_ != VK_NULL_HANDLE; }
    VkRenderPass renderPass() const { return renderPass_; }
    VkExtent2D extent() const { return extent_; }

private:
    VkRenderPass          renderPass_ = VK_NULL_HANDLE;
    VkSampler             sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet       descriptorSet_ = VK_NULL_HANDLE;

    VkImage        colorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory_ = VK_NULL_HANDLE;
    VkImageView    colorView_ = VK_NULL_HANDLE;
    VkImage        depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView    depthView_ = VK_NULL_HANDLE;
    VkFramebuffer  framebuffer_ = VK_NULL_HANDLE;
    VkExtent2D     extent_{};

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline       pipeline_ = VK_NULL_HANDLE;
};

} // namespace dash::vkexp
