#include "rendering/vulkan/FrameGraphLite.h"

#include <cstdio>

namespace dash::vkexp {

bool FrameGraphLite::init(
    VkDevice device,
    uint32_t graphicsQueueFamily,
    VkQueue graphicsQueue,
    VkQueue presentQueue,
    VkSwapchainKHR swapchain,
    VkExtent2D extent,
    VkRenderPass renderPass,
    const std::vector<VkImageView>& imageViews)
{
    device_ = device;
    graphicsQueue_ = graphicsQueue;
    presentQueue_ = presentQueue;
    swapchain_ = swapchain;

    framebuffers_.resize(imageViews.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < imageViews.size(); ++i) {
        VkImageView attachments[] = { imageViews[i] };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            std::fprintf(stderr, "[D78] Failed to create framebuffer %zu.\n", i);
            shutdown();
            return false;
        }
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily;
    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] Failed to create command pool.\n");
        shutdown();
        return false;
    }

    commandBuffers_.resize(imageViews.size(), VK_NULL_HANDLE);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    if (vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()) != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] Failed to allocate command buffers.\n");
        shutdown();
        return false;
    }

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (vkCreateSemaphore(device_, &semInfo, nullptr, &imageAvailable_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] Failed to create imageAvailable semaphore.\n");
        shutdown();
        return false;
    }

    renderFinished_.resize(imageViews.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < renderFinished_.size(); ++i) {
        if (vkCreateSemaphore(device_, &semInfo, nullptr, &renderFinished_[i]) != VK_SUCCESS) {
            std::fprintf(stderr, "[D78] Failed to create renderFinished semaphore %zu.\n", i);
            shutdown();
            return false;
        }
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(device_, &fenceInfo, nullptr, &inFlight_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] Failed to create inFlight fence.\n");
        shutdown();
        return false;
    }

    return true;
}

bool FrameGraphLite::beginFrame(uint32_t& outImageIndex)
{
    vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &inFlight_);

    VkResult acquireResult = vkAcquireNextImageKHR(
        device_,
        swapchain_,
        UINT64_MAX,
        imageAvailable_,
        VK_NULL_HANDLE,
        &outImageIndex);

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "[D78] vkAcquireNextImageKHR failed (%d).\n", static_cast<int>(acquireResult));
        return false;
    }

    vkResetCommandBuffer(commandBuffers_[outImageIndex], 0);
    return true;
}

bool FrameGraphLite::endFrame(uint32_t imageIndex)
{
    const VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailable_;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished_[imageIndex];

    if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlight_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] vkQueueSubmit failed.\n");
        return false;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished_[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(presentQueue_, &presentInfo);
    if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "[D78] vkQueuePresentKHR failed (%d).\n", static_cast<int>(presentResult));
        return false;
    }

    return true;
}

void FrameGraphLite::shutdown()
{
    if (device_ == VK_NULL_HANDLE) return;

    if (inFlight_ != VK_NULL_HANDLE) {
        vkDestroyFence(device_, inFlight_, nullptr);
        inFlight_ = VK_NULL_HANDLE;
    }

    for (VkSemaphore sem : renderFinished_) {
        if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device_, sem, nullptr);
    }
    renderFinished_.clear();

    if (imageAvailable_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, imageAvailable_, nullptr);
        imageAvailable_ = VK_NULL_HANDLE;
    }

    if (commandPool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = VK_NULL_HANDLE;
    }

    for (VkFramebuffer fb : framebuffers_) {
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, fb, nullptr);
    }
    framebuffers_.clear();
    commandBuffers_.clear();

    swapchain_ = VK_NULL_HANDLE;
    graphicsQueue_ = VK_NULL_HANDLE;
    presentQueue_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
}

} // namespace dash::vkexp
