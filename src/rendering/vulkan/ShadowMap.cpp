#include "rendering/vulkan/ShadowMap.h"

#include <array>
#include <cstdio>

#include "rendering/vulkan/PipelineBuilder.h"
#include "rendering/vulkan/VkMath.h"

namespace dash::vkexp {

namespace {

constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

bool createImage(VkPhysicalDevice physicalDevice, VkDevice device,
                 VkImage& outImage, VkDeviceMemory& outMemory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = kDepthFormat;
    imageInfo.extent = {ShadowMap::kResolution, ShadowMap::kResolution, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = kShadowCascades;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &imageInfo, nullptr, &outImage) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device, outImage, &req);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, req.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) return false;
    if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) return false;

    vkBindImageMemory(device, outImage, outMemory, 0);
    return true;
}

// Comparison samplers only get bilinear filtering when the format advertises
// it; MoltenVK does, but falling back keeps the pass correct if it ever stops.
VkFilter pickFilter(VkPhysicalDevice physicalDevice)
{
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice, kDepthFormat, &props);
    const bool linear = (props.optimalTilingFeatures
                         & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    return linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

} // namespace

bool ShadowMap::initTarget(VkPhysicalDevice physicalDevice, VkDevice device)
{
    if (!createImage(physicalDevice, device, image_, memory_)) {
        std::fprintf(stderr, "[Shadow] Failed to create depth image.\n");
        shutdown(device);
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = kDepthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = kShadowCascades;
    if (vkCreateImageView(device, &viewInfo, nullptr, &arrayView_) != VK_SUCCESS) {
        std::fprintf(stderr, "[Shadow] Failed to create depth array view.\n");
        shutdown(device);
        return false;
    }

    // A framebuffer attachment must be a single layer, so each cascade also
    // needs its own 2D view into the array.
    for (int i = 0; i < kShadowCascades; ++i) {
        VkImageViewCreateInfo layerInfo = viewInfo;
        layerInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        layerInfo.subresourceRange.baseArrayLayer = static_cast<uint32_t>(i);
        layerInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &layerInfo, nullptr, &layerViews_[i]) != VK_SUCCESS) {
            std::fprintf(stderr, "[Shadow] Failed to create cascade %d view.\n", i);
            shutdown(device);
            return false;
        }
    }

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = kDepthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthRef;

    // Bracket the pass: wait for last frame's sampling before overwriting, and
    // make this frame's writes visible to the fragment stage of the main pass.
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &depthAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
    rpInfo.pDependencies = deps.data();
    if (vkCreateRenderPass(device, &rpInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        std::fprintf(stderr, "[Shadow] Failed to create depth render pass.\n");
        shutdown(device);
        return false;
    }

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = 1;
    fbInfo.width = kResolution;
    fbInfo.height = kResolution;
    fbInfo.layers = 1;
    for (int i = 0; i < kShadowCascades; ++i) {
        fbInfo.pAttachments = &layerViews_[i];
        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            std::fprintf(stderr, "[Shadow] Failed to create cascade %d framebuffer.\n", i);
            shutdown(device);
            return false;
        }
    }

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = pickFilter(physicalDevice);
    samplerInfo.minFilter = samplerInfo.magFilter;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    // Opaque white border = depth 1.0, so anything sampled outside the light
    // volume compares as "nothing in front of me" and stays lit.
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = samplerInfo.addressModeU;
    samplerInfo.addressModeW = samplerInfo.addressModeU;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "[Shadow] Failed to create comparison sampler.\n");
        shutdown(device);
        return false;
    }

    std::printf("[Shadow] %d cascades ready: %ux%u D32 each.\n",
                kShadowCascades, kResolution, kResolution);
    return true;
}

bool ShadowMap::createPipelines(VkDevice device,
                                const std::string& shaderDir,
                                VkDescriptorSetLayout sceneSetLayout,
                                VkDescriptorSetLayout boneSetLayout)
{
    if (!valid()) return false;

    const VkExtent2D extent{kResolution, kResolution};

    std::string error;
    if (!PipelineBuilder::createShadowDepthPipeline(
            device, extent, renderPass_, VK_NULL_HANDLE, VK_NULL_HANDLE,
            shaderDir + "/shadow_depth.vert.spv",
            pipelineLayout_, pipeline_, error)) {
        std::fprintf(stderr, "[Shadow] Depth pipeline unavailable: %s\n", error.c_str());
        pipelineLayout_ = VK_NULL_HANDLE;
        pipeline_ = VK_NULL_HANDLE;
        return false;
    }

    if (boneSetLayout != VK_NULL_HANDLE) {
        if (!PipelineBuilder::createShadowDepthPipeline(
                device, extent, renderPass_, sceneSetLayout, boneSetLayout,
                shaderDir + "/shadow_depth_skinned.vert.spv",
                skinnedPipelineLayout_, skinnedPipeline_, error)) {
            std::fprintf(stderr, "[Shadow] Skinned depth pipeline unavailable: %s\n", error.c_str());
            skinnedPipelineLayout_ = VK_NULL_HANDLE;
            skinnedPipeline_ = VK_NULL_HANDLE;
        }
    }
    return true;
}

void ShadowMap::beginPass(VkCommandBuffer cmd, int cascade) const
{
    if (!valid() || cascade < 0 || cascade >= kShadowCascades) return;

    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = renderPass_;
    begin.framebuffer = framebuffers_[static_cast<size_t>(cascade)];
    begin.renderArea.offset = {0, 0};
    begin.renderArea.extent = {kResolution, kResolution};
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;

    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
}

void ShadowMap::endPass(VkCommandBuffer cmd) const
{
    if (!valid()) return;
    vkCmdEndRenderPass(cmd);
}

void ShadowMap::shutdown(VkDevice device)
{
    if (device == VK_NULL_HANDLE) return;

    PipelineBuilder::destroy(device, skinnedPipelineLayout_, skinnedPipeline_);
    skinnedPipelineLayout_ = VK_NULL_HANDLE;
    skinnedPipeline_ = VK_NULL_HANDLE;

    PipelineBuilder::destroy(device, pipelineLayout_, pipeline_);
    pipelineLayout_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;

    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    for (VkFramebuffer& fb : framebuffers_) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, fb, nullptr);
            fb = VK_NULL_HANDLE;
        }
    }
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    for (VkImageView& view : layerViews_) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
    }
    if (arrayView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device, arrayView_, nullptr);
        arrayView_ = VK_NULL_HANDLE;
    }
    if (image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device, image_, nullptr);
        image_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
}

} // namespace dash::vkexp
