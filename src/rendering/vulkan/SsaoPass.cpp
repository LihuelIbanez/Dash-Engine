#include "rendering/vulkan/SsaoPass.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include "rendering/mesh/TerrainVertex.h"
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
        std::fprintf(stderr, "[SSAO] vkCreateImage failed.\n");
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
        std::fprintf(stderr, "[SSAO] No device local memory type for attachment.\n");
        return false;
    }
    if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) {
        std::fprintf(stderr, "[SSAO] vkAllocateMemory failed.\n");
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
        std::fprintf(stderr, "[SSAO] vkCreateImageView failed.\n");
        return false;
    }
    return true;
}

bool createFramebuffer(VkDevice device, VkRenderPass renderPass, VkImageView view,
                       VkExtent2D extent, VkFramebuffer& out)
{
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &view;
    fbInfo.width = extent.width;
    fbInfo.height = extent.height;
    fbInfo.layers = 1;
    if (vkCreateFramebuffer(device, &fbInfo, nullptr, &out) != VK_SUCCESS) {
        std::fprintf(stderr, "[SSAO] vkCreateFramebuffer failed.\n");
        return false;
    }
    return true;
}

} // namespace

bool SsaoPass::init(VkDevice device, const std::string& shaderDir)
{
    // ── Depth prepass. Deliberately the same description as ShadowMap's, which
    // is what makes createShadowDepthPipeline usable against it. ─────────────
    {
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
        if (vkCreateRenderPass(device, &rpInfo, nullptr, &depthRenderPass_) != VK_SUCCESS) {
            std::fprintf(stderr, "[SSAO] Failed to create depth prepass.\n");
            return false;
        }
    }

    // ── R8 pass shared by the SSAO estimate and both blur halves ────────────
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = kAoFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        // The fullscreen triangle covers every texel, so nothing is preserved.
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        // Each half of the blur samples what the previous pass wrote, and the
        // scene pass samples the last one, so both directions are needed.
        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
        rpInfo.pDependencies = deps.data();
        if (vkCreateRenderPass(device, &rpInfo, nullptr, &aoRenderPass_) != VK_SUCCESS) {
            std::fprintf(stderr, "[SSAO] Failed to create AO render pass.\n");
            return false;
        }
    }

    // Linear so the full-resolution scene pass can upsample the half-res result;
    // D32_SFLOAT has no guaranteed linear filtering, but every depth tap here is
    // texel aligned, so nearest is both correct and free.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "[SSAO] vkCreateSampler failed.\n");
        return false;
    }

    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &depthSampler_) != VK_SUCCESS) {
        std::fprintf(stderr, "[SSAO] vkCreateSampler (depth) failed.\n");
        return false;
    }

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "[SSAO] Failed to create descriptor set layout.\n");
        return false;
    }

    const uint32_t setCount = static_cast<uint32_t>(sets_.size());
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  setCount * static_cast<uint32_t>(bindings.size())};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = setCount;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "[SSAO] Failed to create descriptor pool.\n");
        return false;
    }

    std::array<VkDescriptorSetLayout, 3> layouts{};
    layouts.fill(setLayout_);
    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = descriptorPool_;
    dsAlloc.descriptorSetCount = setCount;
    dsAlloc.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device, &dsAlloc, sets_.data()) != VK_SUCCESS) {
        std::fprintf(stderr, "[SSAO] Failed to allocate descriptor sets.\n");
        return false;
    }

    // Both share tonemap.vert: a fullscreen triangle with dynamic viewport, so
    // neither pipeline has to be rebuilt when the target resizes.
    std::string error;
    if (!PipelineBuilder::createFullscreenPipeline(
            device, aoRenderPass_, setLayout_,
            shaderDir + "/tonemap.vert.spv", shaderDir + "/ssao.frag.spv", "[SSAO] Estimate",
            ssaoPipelineLayout_, ssaoPipeline_, error)) {
        std::fprintf(stderr, "[SSAO] Estimate pipeline unavailable: %s\n", error.c_str());
        ssaoPipelineLayout_ = VK_NULL_HANDLE;
        ssaoPipeline_ = VK_NULL_HANDLE;
        return false;
    }

    if (!PipelineBuilder::createFullscreenPipeline(
            device, aoRenderPass_, setLayout_,
            shaderDir + "/tonemap.vert.spv", shaderDir + "/ssao_blur.frag.spv", "[SSAO] Blur",
            blurPipelineLayout_, blurPipeline_, error)) {
        std::fprintf(stderr, "[SSAO] Blur pipeline unavailable: %s\n", error.c_str());
        blurPipelineLayout_ = VK_NULL_HANDLE;
        blurPipeline_ = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool SsaoPass::createResources(VkPhysicalDevice physicalDevice, VkDevice device,
                               uint32_t sceneWidth, uint32_t sceneHeight)
{
    if (aoRenderPass_ == VK_NULL_HANDLE || depthRenderPass_ == VK_NULL_HANDLE) return false;
    if (sceneWidth == 0 || sceneHeight == 0) return false;

    extent_ = {std::max(1u, (sceneWidth + 1) / 2), std::max(1u, (sceneHeight + 1) / 2)};

    if (!createAttachment(physicalDevice, device, extent_.width, extent_.height, kDepthFormat,
                          VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                          VK_IMAGE_ASPECT_DEPTH_BIT,
                          depthImage_, depthMemory_, depthView_)) {
        return false;
    }
    if (!createFramebuffer(device, depthRenderPass_, depthView_, extent_, depthFramebuffer_)) {
        return false;
    }

    const VkImageUsageFlags aoUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!createAttachment(physicalDevice, device, extent_.width, extent_.height, kAoFormat,
                          aoUsage, VK_IMAGE_ASPECT_COLOR_BIT,
                          rawImage_, rawMemory_, rawView_) ||
        !createAttachment(physicalDevice, device, extent_.width, extent_.height, kAoFormat,
                          aoUsage, VK_IMAGE_ASPECT_COLOR_BIT,
                          pingImage_, pingMemory_, pingView_) ||
        !createAttachment(physicalDevice, device, extent_.width, extent_.height, kAoFormat,
                          aoUsage, VK_IMAGE_ASPECT_COLOR_BIT,
                          aoImage_, aoMemory_, aoView_)) {
        return false;
    }
    if (!createFramebuffer(device, aoRenderPass_, rawView_, extent_, rawFramebuffer_) ||
        !createFramebuffer(device, aoRenderPass_, pingView_, extent_, pingFramebuffer_) ||
        !createFramebuffer(device, aoRenderPass_, aoView_, extent_, aoFramebuffer_)) {
        return false;
    }

    VkDescriptorImageInfo depthInfo{};
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthInfo.imageView = depthView_;
    depthInfo.sampler = depthSampler_;

    std::array<VkDescriptorImageInfo, 3> sourceInfos{};
    const std::array<VkImageView, 3> sources = {depthView_, rawView_, pingView_};
    for (size_t i = 0; i < sourceInfos.size(); ++i) {
        sourceInfos[i].imageLayout = (i == 0) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                                              : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sourceInfos[i].imageView = sources[i];
        sourceInfos[i].sampler = (i == 0) ? depthSampler_ : sampler_;
    }

    std::array<VkWriteDescriptorSet, 6> writes{};
    for (size_t i = 0; i < sets_.size(); ++i) {
        writes[i * 2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i * 2].dstSet = sets_[i];
        writes[i * 2].dstBinding = 0;
        writes[i * 2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i * 2].descriptorCount = 1;
        writes[i * 2].pImageInfo = &sourceInfos[i];

        writes[i * 2 + 1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i * 2 + 1].dstSet = sets_[i];
        writes[i * 2 + 1].dstBinding = 1;
        writes[i * 2 + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i * 2 + 1].descriptorCount = 1;
        writes[i * 2 + 1].pImageInfo = &depthInfo;
    }
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    std::printf("[SSAO] Ready: %ux%u R8 (half of %ux%u).\n",
                extent_.width, extent_.height, sceneWidth, sceneHeight);
    return true;
}

bool SsaoPass::createPipelines(VkDevice device, const std::string& shaderDir,
                               VkDescriptorSetLayout sceneSetLayout,
                               VkDescriptorSetLayout boneSetLayout)
{
    if (!valid()) return false;
    // The viewport is baked into these, so they belong to the resources and are
    // rebuilt on every resize alongside the images.
    std::string error;
    if (!PipelineBuilder::createShadowDepthPipeline(
            device, extent_, depthRenderPass_, VK_NULL_HANDLE, VK_NULL_HANDLE,
            shaderDir + "/shadow_depth.vert.spv",
            depthPipelineLayout_, depthPipeline_, error, 0, "SSAO prepass")) {
        std::fprintf(stderr, "[SSAO] Depth prepass pipeline unavailable: %s\n", error.c_str());
        depthPipelineLayout_ = VK_NULL_HANDLE;
        depthPipeline_ = VK_NULL_HANDLE;
        return false;
    }

    if (boneSetLayout != VK_NULL_HANDLE) {
        if (!PipelineBuilder::createShadowDepthPipeline(
                device, extent_, depthRenderPass_, sceneSetLayout, boneSetLayout,
                shaderDir + "/shadow_depth_skinned.vert.spv",
                skinnedPipelineLayout_, skinnedPipeline_, error, 0, "SSAO skinned prepass")) {
            std::fprintf(stderr, "[SSAO] Skinned prepass pipeline unavailable: %s\n", error.c_str());
            skinnedPipelineLayout_ = VK_NULL_HANDLE;
            skinnedPipeline_ = VK_NULL_HANDLE;
            return false;
        }
    }

    // Same vertex shader, wider stream: the terrain vertex also keeps position
    // at offset 0.
    if (!PipelineBuilder::createShadowDepthPipeline(
            device, extent_, depthRenderPass_, VK_NULL_HANDLE, VK_NULL_HANDLE,
            shaderDir + "/shadow_depth.vert.spv",
            terrainPipelineLayout_, terrainPipeline_, error,
            static_cast<uint32_t>(sizeof(TerrainVkVertex)), "SSAO terrain prepass")) {
        std::fprintf(stderr, "[SSAO] Terrain prepass pipeline unavailable: %s\n", error.c_str());
        terrainPipelineLayout_ = VK_NULL_HANDLE;
        terrainPipeline_ = VK_NULL_HANDLE;
        return false;
    }

    if (sceneSetLayout != VK_NULL_HANDLE) {
        if (!PipelineBuilder::createShadowBillboardPipeline(
                device, extent_, depthRenderPass_, sceneSetLayout,
                shaderDir + "/shadow_billboard.vert.spv",
                shaderDir + "/shadow_billboard.frag.spv",
                billboardPipelineLayout_, billboardPipeline_, error)) {
            std::fprintf(stderr, "[SSAO] Billboard prepass pipeline unavailable: %s\n", error.c_str());
            billboardPipelineLayout_ = VK_NULL_HANDLE;
            billboardPipeline_ = VK_NULL_HANDLE;
            return false;
        }
    }

    return true;
}

void SsaoPass::destroyDepthPipelines(VkDevice device)
{
    PipelineBuilder::destroy(device, billboardPipelineLayout_, billboardPipeline_);
    billboardPipelineLayout_ = VK_NULL_HANDLE;
    billboardPipeline_ = VK_NULL_HANDLE;

    PipelineBuilder::destroy(device, terrainPipelineLayout_, terrainPipeline_);
    terrainPipelineLayout_ = VK_NULL_HANDLE;
    terrainPipeline_ = VK_NULL_HANDLE;

    PipelineBuilder::destroy(device, skinnedPipelineLayout_, skinnedPipeline_);
    skinnedPipelineLayout_ = VK_NULL_HANDLE;
    skinnedPipeline_ = VK_NULL_HANDLE;

    PipelineBuilder::destroy(device, depthPipelineLayout_, depthPipeline_);
    depthPipelineLayout_ = VK_NULL_HANDLE;
    depthPipeline_ = VK_NULL_HANDLE;
}

void SsaoPass::destroyResources(VkDevice device)
{
    if (device == VK_NULL_HANDLE) return;

    destroyDepthPipelines(device);

    const auto dropColor = [device](VkFramebuffer& fb, VkImageView& view,
                                    VkImage& image, VkDeviceMemory& memory) {
        if (fb     != VK_NULL_HANDLE) { vkDestroyFramebuffer(device, fb, nullptr);  fb = VK_NULL_HANDLE; }
        if (view   != VK_NULL_HANDLE) { vkDestroyImageView(device, view, nullptr);  view = VK_NULL_HANDLE; }
        if (image  != VK_NULL_HANDLE) { vkDestroyImage(device, image, nullptr);     image = VK_NULL_HANDLE; }
        if (memory != VK_NULL_HANDLE) { vkFreeMemory(device, memory, nullptr);      memory = VK_NULL_HANDLE; }
    };

    dropColor(aoFramebuffer_, aoView_, aoImage_, aoMemory_);
    dropColor(pingFramebuffer_, pingView_, pingImage_, pingMemory_);
    dropColor(rawFramebuffer_, rawView_, rawImage_, rawMemory_);
    dropColor(depthFramebuffer_, depthView_, depthImage_, depthMemory_);

    extent_ = {};
}

void SsaoPass::shutdown(VkDevice device)
{
    if (device == VK_NULL_HANDLE) return;

    destroyResources(device);

    PipelineBuilder::destroy(device, blurPipelineLayout_, blurPipeline_);
    blurPipelineLayout_ = VK_NULL_HANDLE;
    blurPipeline_ = VK_NULL_HANDLE;

    PipelineBuilder::destroy(device, ssaoPipelineLayout_, ssaoPipeline_);
    ssaoPipelineLayout_ = VK_NULL_HANDLE;
    ssaoPipeline_ = VK_NULL_HANDLE;

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
        sets_.fill(VK_NULL_HANDLE);
    }
    if (setLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
        setLayout_ = VK_NULL_HANDLE;
    }
    if (depthSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device, depthSampler_, nullptr);
        depthSampler_ = VK_NULL_HANDLE;
    }
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }
    if (aoRenderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, aoRenderPass_, nullptr);
        aoRenderPass_ = VK_NULL_HANDLE;
    }
    if (depthRenderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, depthRenderPass_, nullptr);
        depthRenderPass_ = VK_NULL_HANDLE;
    }
}

void SsaoPass::beginDepthPass(VkCommandBuffer cmd) const
{
    if (depthFramebuffer_ == VK_NULL_HANDLE) return;

    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = depthRenderPass_;
    begin.framebuffer = depthFramebuffer_;
    begin.renderArea.offset = {0, 0};
    begin.renderArea.extent = extent_;
    begin.clearValueCount = 1;
    begin.pClearValues = &clear;

    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
}

void SsaoPass::endDepthPass(VkCommandBuffer cmd) const
{
    if (depthFramebuffer_ == VK_NULL_HANDLE) return;
    vkCmdEndRenderPass(cmd);
}

void SsaoPass::drawFullscreen(VkCommandBuffer cmd, VkFramebuffer target,
                              VkPipeline pipeline, VkPipelineLayout layout,
                              VkDescriptorSet set, const float (&pushConstants)[16]) const
{
    VkRenderPassBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = aoRenderPass_;
    begin.framebuffer = target;
    begin.renderArea.offset = {0, 0};
    begin.renderArea.extent = extent_;
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent_.width);
    viewport.height = static_cast<float>(extent_.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(pushConstants), pushConstants);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
}

void SsaoPass::recordResolve(VkCommandBuffer cmd, const SsaoParams& params,
                             float projXX, float projYY, float zNear, float zFar) const
{
    if (!valid() || ssaoPipeline_ == VK_NULL_HANDLE || blurPipeline_ == VK_NULL_HANDLE) return;

    const float invW = 1.0f / static_cast<float>(extent_.width);
    const float invH = 1.0f / static_cast<float>(extent_.height);

    float pc[16]{};
    pc[0] = projXX;
    pc[1] = projYY;
    pc[2] = zNear;
    pc[3] = zFar;
    pc[4] = std::max(params.radius, 1e-3f);
    // Disabled still runs the pass, so the image the scene samples is a clean
    // 1.0 instead of whatever the previous frame left behind.
    pc[5] = params.enabled ? params.intensity : 0.0f;
    pc[6] = params.bias;
    pc[7] = params.power;
    pc[8] = invW;
    pc[9] = invH;
    pc[10] = static_cast<float>(extent_.width);
    pc[11] = static_cast<float>(extent_.height);
    drawFullscreen(cmd, rawFramebuffer_, ssaoPipeline_, ssaoPipelineLayout_, sets_[0], pc);

    // Depth tolerance scaled with the radius: the blur must not cross an edge
    // the occlusion itself could not have reached over.
    const float tolerance = std::max(params.radius, 1e-3f);

    float blurPc[16]{};
    blurPc[2] = tolerance;
    blurPc[6] = zNear;
    blurPc[7] = zFar;

    blurPc[0] = invW;
    blurPc[1] = 0.0f;
    drawFullscreen(cmd, pingFramebuffer_, blurPipeline_, blurPipelineLayout_, sets_[1], blurPc);

    blurPc[0] = 0.0f;
    blurPc[1] = invH;
    drawFullscreen(cmd, aoFramebuffer_, blurPipeline_, blurPipelineLayout_, sets_[2], blurPc);
}

} // namespace dash::vkexp
