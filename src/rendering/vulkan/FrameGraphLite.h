#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

namespace dash::vkexp {

class FrameGraphLite {
public:
    FrameGraphLite() = default;
    ~FrameGraphLite() = default;

    FrameGraphLite(const FrameGraphLite&) = delete;
    FrameGraphLite& operator=(const FrameGraphLite&) = delete;

    bool init(
        VkDevice device,
        uint32_t graphicsQueueFamily,
        VkQueue graphicsQueue,
        VkQueue presentQueue,
        VkSwapchainKHR swapchain,
        VkExtent2D extent,
        VkRenderPass renderPass,
        const std::vector<VkImageView>& imageViews);

    void shutdown();

    bool beginFrame(uint32_t& outImageIndex);
    bool endFrame(uint32_t imageIndex);

    VkCommandBuffer commandBuffer(uint32_t imageIndex) const { return commandBuffers_[imageIndex]; }
    VkFramebuffer framebuffer(uint32_t imageIndex) const { return framebuffers_[imageIndex]; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    std::vector<VkFramebuffer> framebuffers_;

    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    std::vector<VkSemaphore> renderFinished_;
    VkFence inFlight_ = VK_NULL_HANDLE;
};

} // namespace dash::vkexp
