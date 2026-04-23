#include "rendering/vulkan/SwapchainContext.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include <GLFW/glfw3.h>

namespace dash::vkexp {

SwapchainContext::~SwapchainContext() = default;

VkSurfaceFormatKHR SwapchainContext::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const
{
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats[0];
}

VkPresentModeKHR SwapchainContext::choosePresentMode(const std::vector<VkPresentModeKHR>& presentModes) const
{
    for (VkPresentModeKHR mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapchainContext::chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window) const
{
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };

    actualExtent.width = std::max(capabilities.minImageExtent.width,
        std::min(capabilities.maxImageExtent.width, actualExtent.width));
    actualExtent.height = std::max(capabilities.minImageExtent.height,
        std::min(capabilities.maxImageExtent.height, actualExtent.height));

    return actualExtent;
}

bool SwapchainContext::init(const DeviceContext& deviceContext, VkSurfaceKHR surface, GLFWwindow* window)
{
    VkPhysicalDevice physicalDevice = deviceContext.physicalDevice();
    VkDevice device = deviceContext.device();

    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    if (formatCount == 0) {
        std::fprintf(stderr, "[D73] No surface formats available.\n");
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    if (presentModeCount == 0) {
        std::fprintf(stderr, "[D73] No present modes available.\n");
        return false;
    }
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

    VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
    VkPresentModeKHR presentMode = choosePresentMode(presentModes);
    extent_ = chooseExtent(capabilities, window);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    auto qf = deviceContext.queueFamilies();
    uint32_t queueFamilyIndices[] = { qf.graphicsFamily.value(), qf.presentFamily.value() };

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (qf.graphicsFamily != qf.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D73] vkCreateSwapchainKHR failed.\n");
        return false;
    }

    vkGetSwapchainImagesKHR(device, swapchain_, &imageCount, nullptr);
    images_.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain_, &imageCount, images_.data());

    imageFormat_ = surfaceFormat.format;

    imageViews_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = imageFormat_;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &imageViews_[i]) != VK_SUCCESS) {
            std::fprintf(stderr, "[D73] vkCreateImageView failed for image %zu.\n", i);
            return false;
        }
    }

    // Create depth resources
    if (!createDepthResources(physicalDevice, device)) {
        std::fprintf(stderr, "[D73] Failed to create depth resources.\n");
        return false;
    }

    // Color attachment
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = imageFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Depth attachment
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D73] vkCreateRenderPass failed.\n");
        return false;
    }

    std::fprintf(stdout, "[D73] Swapchain + render pass created successfully.\n");
    return true;
}

uint32_t SwapchainContext::findMemoryType(VkPhysicalDevice physicalDevice,
                                           uint32_t typeFilter,
                                           VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) &&
            ((memProps.memoryTypes[i].propertyFlags & properties) == properties)) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool SwapchainContext::createDepthResources(VkPhysicalDevice physicalDevice, VkDevice device)
{
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = depthFormat;
    imageInfo.extent = {extent_.width, extent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &imageInfo, nullptr, &depthImage_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D73] Failed to create depth image.\n");
        return false;
    }

    VkMemoryRequirements memReqs{};
    vkGetImageMemoryRequirements(device, depthImage_, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        std::fprintf(stderr, "[D73] No suitable memory type for depth image.\n");
        return false;
    }

    if (vkAllocateMemory(device, &allocInfo, nullptr, &depthMemory_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D73] Failed to allocate depth image memory.\n");
        return false;
    }

    vkBindImageMemory(device, depthImage_, depthMemory_, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &depthImageView_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D73] Failed to create depth image view.\n");
        return false;
    }

    std::fprintf(stdout, "[D73] Depth buffer created successfully.\n");
    return true;
}

void SwapchainContext::shutdown(VkDevice device)
{
    if (depthImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, depthImage_, nullptr);
        depthImage_ = VK_NULL_HANDLE;
    }
    if (depthMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, depthMemory_, nullptr);
        depthMemory_ = VK_NULL_HANDLE;
    }

    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }

    for (VkImageView view : imageViews_) {
        vkDestroyImageView(device, view, nullptr);
    }
    imageViews_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }

    images_.clear();
}

} // namespace dash::vkexp
