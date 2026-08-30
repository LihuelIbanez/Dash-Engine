#include "rendering/vulkan/HdrTarget.h"

#include <array>
#include <cstdio>

#include "rendering/vulkan/PipelineBuilder.h"

namespace dash::vkexp {

namespace {

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter,
                        VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool createAttachment(VkPhysicalDevice physicalDevice, VkDevice device,
                      uint32_t width, uint32_t height, VkFormat format,
                      VkImageUsageFlags usage, VkImageAspectFlags aspect,
                      VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &imageInfo, nullptr, &outImage) != VK_SUCCESS) {
        std::fprintf(stderr, "[HDR] vkCreateImage failed.\n");
        return false;
    }

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device, outImage, &req);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, req.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        std::fprintf(stderr, "[HDR] No device local memory type for attachment.\n");
        return false;
    }
    if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) {
        std::fprintf(stderr, "[HDR] vkAllocateMemory failed.\n");
        return false;
    }
    vkBindImageMemory(device, outImage, outMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = outImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {aspect, 0, 1, 0, 1};
    if (vkCreateImageView(device, &viewInfo, nullptr, &outView) != VK_SUCCESS) {
        std::fprintf(stderr, "[HDR] vkCreateImageView failed.\n");
        return false;
    }
    return true;
}

} // namespace

bool HdrTarget::init(VkDevice device)
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = kColorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = kDepthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // The second dependency is the one that matters: the tonemap pass samples
    // this attachment in the very next render pass of the same command buffer.
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpInfo.pAttachments = attachments.data();
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
    rpInfo.pDependencies = deps.data();
    if (vkCreateRenderPass(device, &rpInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        std::fprintf(stderr, "[HDR] vkCreateRenderPass failed.\n");
        return false;
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "[HDR] vkCreateSampler failed.\n");
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "[HDR] Failed to create tonemap descriptor set layout.\n");
        return false;
    }

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "[HDR] Failed to create tonemap descriptor pool.\n");
        return false;
    }

    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = descriptorPool_;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &setLayout_;
    if (vkAllocateDescriptorSets(device, &dsAlloc, &descriptorSet_) != VK_SUCCESS) {
        std::fprintf(stderr, "[HDR] Failed to allocate tonemap descriptor set.\n");
        return false;
    }

    return true;
}

bool HdrTarget::createResources(VkPhysicalDevice physicalDevice, VkDevice device,
                                uint32_t width, uint32_t height)
{
    if (renderPass_ == VK_NULL_HANDLE) return false;
    if (width == 0 || height == 0) return false;

    if (!createAttachment(physicalDevice, device, width, height, kColorFormat,
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_IMAGE_ASPECT_COLOR_BIT,
                          colorImage_, colorMemory_, colorView_)) {
        return false;
    }
    if (!createAttachment(physicalDevice, device, width, height, kDepthFormat,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                          VK_IMAGE_ASPECT_DEPTH_BIT,
                          depthImage_, depthMemory_, depthView_)) {
        return false;
    }

    std::array<VkImageView, 2> views = {colorView_, depthView_};
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = static_cast<uint32_t>(views.size());
    fbInfo.pAttachments = views.data();
    fbInfo.width = width;
    fbInfo.height = height;
    fbInfo.layers = 1;
    if (vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffer_) != VK_SUCCESS) {
        std::fprintf(stderr, "[HDR] vkCreateFramebuffer failed.\n");
        return false;
    }

    extent_ = {width, height};

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = colorView_;
    imageInfo.sampler = sampler_;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    return true;
}

void HdrTarget::destroyResources(VkDevice device)
{
    if (device == VK_NULL_HANDLE) return;

    if (framebuffer_ != VK_NULL_HANDLE) { vkDestroyFramebuffer(device, framebuffer_, nullptr); framebuffer_ = VK_NULL_HANDLE; }
    if (depthView_   != VK_NULL_HANDLE) { vkDestroyImageView(device, depthView_, nullptr);     depthView_ = VK_NULL_HANDLE; }
    if (depthImage_  != VK_NULL_HANDLE) { vkDestroyImage(device, depthImage_, nullptr);        depthImage_ = VK_NULL_HANDLE; }
    if (depthMemory_ != VK_NULL_HANDLE) { vkFreeMemory(device, depthMemory_, nullptr);         depthMemory_ = VK_NULL_HANDLE; }
    if (colorView_   != VK_NULL_HANDLE) { vkDestroyImageView(device, colorView_, nullptr);     colorView_ = VK_NULL_HANDLE; }
    if (colorImage_  != VK_NULL_HANDLE) { vkDestroyImage(device, colorImage_, nullptr);        colorImage_ = VK_NULL_HANDLE; }
    if (colorMemory_ != VK_NULL_HANDLE) { vkFreeMemory(device, colorMemory_, nullptr);         colorMemory_ = VK_NULL_HANDLE; }

    extent_ = {};
}

bool HdrTarget::createPipeline(VkDevice device, VkRenderPass outputRenderPass,
                               const std::string& shaderDir)
{
    std::string error;
    if (!PipelineBuilder::createTonemapPipeline(
            device, outputRenderPass, setLayout_,
            shaderDir + "/tonemap.vert.spv", shaderDir + "/tonemap.frag.spv",
            pipelineLayout_, pipeline_, error)) {
        std::fprintf(stderr, "[HDR] Tonemap pipeline creation failed: %s\n", error.c_str());
        pipeline_ = VK_NULL_HANDLE;
        pipelineLayout_ = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void HdrTarget::beginPass(VkCommandBuffer cmd, const float clearColor[4]) const
{
    VkClearValue clears[2]{};
    for (int i = 0; i < 4; ++i) clears[0].color.float32[i] = clearColor[i];
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = renderPass_;
    begin.framebuffer = framebuffer_;
    begin.renderArea.offset = {0, 0};
    begin.renderArea.extent = extent_;
    begin.clearValueCount = 2;
    begin.pClearValues = clears;

    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
}

void HdrTarget::endPass(VkCommandBuffer cmd) const
{
    vkCmdEndRenderPass(cmd);
}

void HdrTarget::drawTonemap(VkCommandBuffer cmd, const GradingParams& grading,
                            bool encodeSrgb, const float* flashPremulRgb) const
{
    if (pipeline_ == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                            0, 1, &descriptorSet_, 0, nullptr);

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    float pc[kTonemapPushConstantFloats];
    packTonemapPushConstants(grading, encodeSrgb, pc, flashPremulRgb);
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pc), pc);

    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void HdrTarget::shutdown(VkDevice device)
{
    if (device == VK_NULL_HANDLE) return;

    destroyResources(device);

    PipelineBuilder::destroy(device, pipelineLayout_, pipeline_);
    pipelineLayout_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
        descriptorSet_ = VK_NULL_HANDLE;
    }
    if (setLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
        setLayout_ = VK_NULL_HANDLE;
    }
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
}

} // namespace dash::vkexp
