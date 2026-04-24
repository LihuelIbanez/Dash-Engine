#pragma once

#include <vector>

#include <vulkan/vulkan.h>

#include "rendering/vulkan/DeviceContext.h"

struct GLFWwindow;

namespace dash::vkexp {

class SwapchainContext {
public:
    SwapchainContext() = default;
    ~SwapchainContext();

    SwapchainContext(const SwapchainContext&) = delete;
    SwapchainContext& operator=(const SwapchainContext&) = delete;

    bool init(const DeviceContext& deviceContext, VkSurfaceKHR surface, GLFWwindow* window);
    bool init(const DeviceContext& deviceContext, VkSurfaceKHR surface, uint32_t width, uint32_t height);
    void shutdown(VkDevice device);

    VkSwapchainKHR swapchain() const { return swapchain_; }
    VkFormat imageFormat() const { return imageFormat_; }
    VkExtent2D extent() const { return extent_; }
    VkRenderPass renderPass() const { return renderPass_; }
    const std::vector<VkImageView>& imageViews() const { return imageViews_; }
    uint32_t imageCount() const { return static_cast<uint32_t>(images_.size()); }
    VkImageView depthImageView() const { return depthImageView_; }

private:
    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& presentModes) const;
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window) const;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    VkFormat imageFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent_{};
    VkRenderPass renderPass_ = VK_NULL_HANDLE;

    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;

    bool createDepthResources(VkPhysicalDevice physicalDevice, VkDevice device);
    uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                            VkMemoryPropertyFlags properties) const;
};

} // namespace dash::vkexp
