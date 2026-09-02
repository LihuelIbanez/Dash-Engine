#include "EditorVkContext.h"
#include "rendering/vulkan/PipelineBuilder.h"
#include "rendering/mesh/ProceduralMeshUpload.h"
#include "rendering/mesh/TerrainVertex.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_sdl2.h"
#include <SDL2/SDL_vulkan.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <array>
#include <cmath>

#include "game/rendering/stb_image.h"

using namespace dash::vkexp;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static uint32_t findMemType(VkPhysicalDevice pd, uint32_t filter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return UINT32_MAX;
}

static bool createBuffer(VkPhysicalDevice pd, VkDevice dev, VkDeviceSize size,
                          VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps,
                          VkBuffer& buf, VkDeviceMemory& mem)
{
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &ci, nullptr, &buf) != VK_SUCCESS) return false;
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev, buf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemType(pd, req.memoryTypeBits, memProps);
    if (ai.memoryTypeIndex == UINT32_MAX) return false;
    if (vkAllocateMemory(dev, &ai, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindBufferMemory(dev, buf, mem, 0);
    return true;
}

static bool createImage(VkPhysicalDevice pd, VkDevice dev, uint32_t w, uint32_t h,
                         VkFormat fmt, VkImageUsageFlags usage,
                         VkImage& img, VkDeviceMemory& mem)
{
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D; ci.format = fmt;
    ci.extent = {w, h, 1}; ci.mipLevels = 1; ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(dev, &ci, nullptr, &img) != VK_SUCCESS) return false;
    VkMemoryRequirements req; vkGetImageMemoryRequirements(dev, img, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemType(pd, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) return false;
    if (vkAllocateMemory(dev, &ai, nullptr, &mem) != VK_SUCCESS) return false;
    vkBindImageMemory(dev, img, mem, 0);
    return true;
}

static VkImageView createView(VkDevice dev, VkImage img, VkFormat fmt, VkImageAspectFlags aspect)
{
    VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ci.image = img; ci.viewType = VK_IMAGE_VIEW_TYPE_2D; ci.format = fmt;
    ci.subresourceRange = {aspect, 0, 1, 0, 1};
    VkImageView v = VK_NULL_HANDLE;
    vkCreateImageView(dev, &ci, nullptr, &v);
    return v;
}

static void checkVkResult(VkResult r) {
    if (r != VK_SUCCESS)
        std::fprintf(stderr, "[EditorVk] VkResult = %d\n", static_cast<int>(r));
}

// TEMP-VERIFY
#include "stb_image_write.h"
static void tempDumpViewport(VkPhysicalDevice pd, VkDevice dev, VkQueue queue,
                             uint32_t queueFamily, VkImage image,
                             uint32_t w, uint32_t h, const char* path)
{
    vkDeviceWaitIdle(dev);
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * h * 4;

    VkBuffer buf = VK_NULL_HANDLE; VkDeviceMemory mem = VK_NULL_HANDLE;
    if (!createBuffer(pd, dev, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      buf, mem)) {
        std::fprintf(stderr, "[Dump] buffer failed\n");
        return;
    }

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = queueFamily;
    VkCommandPool pool = VK_NULL_HANDLE;
    vkCreateCommandPool(dev, &pci, nullptr, &pool);

    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(dev, &cai, &cmd);

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &region);

    std::swap(b.oldLayout, b.newLayout);
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    void* mapped = nullptr;
    if (vkMapMemory(dev, mem, 0, bytes, 0, &mapped) == VK_SUCCESS) {
        auto* px = static_cast<unsigned char*>(mapped);
        for (VkDeviceSize i = 0; i < bytes; i += 4) std::swap(px[i], px[i + 2]);
        stbi_write_png(path, static_cast<int>(w), static_cast<int>(h), 4, px,
                       static_cast<int>(w) * 4);
        vkUnmapMemory(dev, mem);
        std::fprintf(stdout, "[Dump] wrote %s (%ux%u)\n", path, w, h);
    }

    vkDestroyCommandPool(dev, pool, nullptr);
    vkDestroyBuffer(dev, buf, nullptr);
    vkFreeMemory(dev, mem, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// init
// ─────────────────────────────────────────────────────────────────────────────
bool EditorVkContext::createInstance(SDL_Window* window)
{
    unsigned int extCount = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr);
    std::vector<const char*> exts(extCount);
    SDL_Vulkan_GetInstanceExtensions(window, &extCount, exts.data());

#ifdef __APPLE__
    exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    exts.push_back("VK_KHR_get_physical_device_properties2");
#endif

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "DashEngine Editor";
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
#ifdef __APPLE__
    ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    return vkCreateInstance(&ci, nullptr, &instance_) == VK_SUCCESS;
}

bool EditorVkContext::init(SDL_Window* window)
{
    if (!createInstance(window)) {
        std::fprintf(stderr, "[EditorVk] Failed to create Vulkan instance.\n");
        return false;
    }

    if (!SDL_Vulkan_CreateSurface(window, instance_, &surface_)) {
        std::fprintf(stderr, "[EditorVk] SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        return false;
    }

    if (!deviceCtx_.init(instance_, surface_)) {
        std::fprintf(stderr, "[EditorVk] Failed to init device context.\n");
        return false;
    }

    int w = 0, h = 0;
    SDL_Vulkan_GetDrawableSize(window, &w, &h);
    if (!swapchain_.init(deviceCtx_, surface_, static_cast<uint32_t>(w), static_cast<uint32_t>(h))) {
        std::fprintf(stderr, "[EditorVk] Failed to create swapchain.\n");
        return false;
    }

    auto qf = deviceCtx_.queueFamilies();
    if (!frameGraph_.init(deviceCtx_.device(),
                          qf.graphicsFamily.value(),
                          deviceCtx_.graphicsQueue(),
                          deviceCtx_.presentQueue(),
                          swapchain_.swapchain(),
                          swapchain_.extent(),
                          swapchain_.renderPass(),
                          swapchain_.imageViews(),
                          swapchain_.depthImageView())) {
        std::fprintf(stderr, "[EditorVk] Failed to init frame graph.\n");
        return false;
    }

    // ── Sampler (needed by scene descriptors and viewport texture) ───────────
    {
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci.magFilter = VK_FILTER_LINEAR; sci.minFilter = VK_FILTER_LINEAR;
        sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(deviceCtx_.device(), &sci, nullptr, &vpSampler_) != VK_SUCCESS)
            return false;
    }

    // ── Terrain texture array (load before descriptors) ───────────────────
    createTerrainTextureArray(); // non-fatal: falls back to dummy if it fails

    // Before the descriptors: whether the depth target came up decides if the
    // scene layout declares binding 3 and which fragment variants are loaded.
    shadowMap_.initTarget(deviceCtx_.physicalDevice(), deviceCtx_.device());

    // ── Scene descriptors (UBO for camera) ──────────────────────────────────
    if (!createSceneDescriptors()) return false;

    // Before every pipeline: the skinned scene pipeline and both depth passes
    // take boneSetLayout_ as their set 1.
    if (!createBoneResources()) {
        std::fprintf(stderr, "[EditorVk] Bone palette unavailable; skinned meshes draw in bind pose.\n");
        destroyBoneResources();
    }

    // ── HDR scene target: every viewport pipeline targets this pass ─────────
    if (!hdr_.init(deviceCtx_.device())) {
        std::fprintf(stderr, "[EditorVk] HDR target init failed.\n");
        return false;
    }

    // Non-fatal: the viewport still renders correctly without combat VFX.
    if (!particles_.init(deviceCtx_.physicalDevice(), deviceCtx_.device(),
                         deviceCtx_.graphicsQueue(), frameGraph_.commandPool(),
                         hdr_.renderPass(), swapchain_.imageCount(), VULKAN_SHADER_DIR)) {
        std::fprintf(stderr, "[EditorVk] Particle renderer unavailable; combat VFX disabled.\n");
    }

    // ── Offscreen resolve pass (tonemap output, sampled by ImGui) ───────────
    {
        VkAttachmentDescription colorAtt{};
        colorAtt.format = VK_FORMAT_B8G8R8A8_UNORM;
        colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        // The fullscreen triangle covers every texel, so nothing is preserved.
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &colorRef;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpci.attachmentCount = 1; rpci.pAttachments = &colorAtt;
        rpci.subpassCount = 1; rpci.pSubpasses = &sub;
        rpci.dependencyCount = static_cast<uint32_t>(deps.size());
        rpci.pDependencies = deps.data();
        if (vkCreateRenderPass(deviceCtx_.device(), &rpci, nullptr, &vpRenderPass_) != VK_SUCCESS)
            return false;
    }

    // ── Pipelines ───────────────────────────────────────────────────────────
    if (!createPipelines()) return false;

    if (!hdr_.createPipeline(deviceCtx_.device(), vpRenderPass_, VULKAN_SHADER_DIR)) {
        std::fprintf(stderr, "[EditorVk] Tonemap pipeline unavailable.\n");
        return false;
    }

    // Depth-only casters. Skinned casters follow the animated pose when the bone
    // palette came up; otherwise they fall back to their bind-pose silhouette.
    if (shadowMap_.valid()) {
        shadowMap_.createPipelines(deviceCtx_.device(), VULKAN_SHADER_DIR,
                                   sceneDescLayout_, boneSetLayout_);
    }

    ssao_.init(deviceCtx_.device(), VULKAN_SHADER_DIR);

    // ── Cube mesh (for entity rendering) ────────────────────────────────────
    cubeMeshBuf_.initCube(deviceCtx_.physicalDevice(), deviceCtx_.device());

    // ── ImGui Vulkan backend init ───────────────────────────────────────────
    {
        ImGui_ImplVulkan_InitInfo info{};
        info.ApiVersion = VK_API_VERSION_1_0;
        info.Instance = instance_;
        info.PhysicalDevice = deviceCtx_.physicalDevice();
        info.Device = deviceCtx_.device();
        info.QueueFamily = qf.graphicsFamily.value();
        info.Queue = deviceCtx_.graphicsQueue();
        info.DescriptorPoolSize = 64;
        info.MinImageCount = swapchain_.imageCount();
        info.ImageCount = swapchain_.imageCount();
        info.PipelineInfoMain.RenderPass = swapchain_.renderPass();
        info.CheckVkResultFn = checkVkResult;
        if (!ImGui_ImplVulkan_Init(&info)) {
            std::fprintf(stderr, "[EditorVk] ImGui_ImplVulkan_Init failed.\n");
            return false;
        }
    }

    // Create initial offscreen target
    createOffscreenTarget(1280, 720);

    std::fprintf(stdout, "[EditorVk] Vulkan context initialized.\n");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Terrain texture arrays (shared with the runtime renderer)
// ─────────────────────────────────────────────────────────────────────────────
bool EditorVkContext::createTerrainTextureArray()
{
    return createTerrainTextureSet(deviceCtx_.physicalDevice(),
                                   deviceCtx_.device(),
                                   deviceCtx_.graphicsQueue(),
                                   frameGraph_.commandPool(),
                                   defaultTerrainTextureRoot(),
                                   terrainTextures_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene descriptors (camera UBO at binding 0, dummy sampler at binding 1,
// scene lights UBO at binding 2, terrain arrays at bindings 4/6)
// ─────────────────────────────────────────────────────────────────────────────
bool EditorVkContext::createSceneDescriptors()
{
    VkDevice dev = deviceCtx_.device();

    // Layout: binding 0 = UBO, binding 1 = combined image sampler (required by terrain shader)
    VkDescriptorSetLayoutBinding uboB{};
    uboB.binding = 0; uboB.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboB.descriptorCount = 1; uboB.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding samplerB{};
    samplerB.binding = 1; samplerB.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerB.descriptorCount = 1; samplerB.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Matches the runtime layout so the *_lit fragment variants are usable here.
    VkDescriptorSetLayoutBinding lightsB{};
    lightsB.binding = 2; lightsB.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightsB.descriptorCount = 1; lightsB.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Only declared when the depth target exists: the "_lit" variants used
    // otherwise never reference it.
    VkDescriptorSetLayoutBinding shadowB{};
    shadowB.binding = 3; shadowB.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowB.descriptorCount = 1; shadowB.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    // MoltenVK reports mutableComparisonSamplers as unsupported, so a sampler
    // with compareEnable may only reach a shader as an immutable one.
    const VkSampler shadowSampler = shadowMap_.sampler();
    shadowB.pImmutableSamplers = &shadowSampler;

    VkDescriptorSetLayoutBinding terrainAlbedoB{};
    terrainAlbedoB.binding = 4; terrainAlbedoB.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    terrainAlbedoB.descriptorCount = 1; terrainAlbedoB.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // scene_lights.glsl includes ssao_sample.glsl unconditionally, so every
    // fragment shader built here declares binding 5 whether the viewport
    // computes SSAO or not. It points at the white dummy, which reads as 1.0.
    VkDescriptorSetLayoutBinding ssaoB{};
    ssaoB.binding = 5; ssaoB.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssaoB.descriptorCount = 1; ssaoB.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding terrainNormalB{};
    terrainNormalB.binding = 6; terrainNormalB.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    terrainNormalB.descriptorCount = 1; terrainNormalB.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkDescriptorSetLayoutBinding> bindings = {uboB, samplerB, lightsB};
    if (shadowMap_.valid()) bindings.push_back(shadowB);
    bindings.push_back(terrainAlbedoB);
    bindings.push_back(ssaoB);
    bindings.push_back(terrainNormalB);

    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = static_cast<uint32_t>(bindings.size()); lci.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(dev, &lci, nullptr, &sceneDescLayout_) != VK_SUCCESS)
        return false;

    // The shadow map is a fourth combined sampler in the set; undercounting here
    // makes vkAllocateDescriptorSets fail and the viewport come up empty.
    const uint32_t samplersPerSet = shadowMap_.valid() ? 5u : 4u;
    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, samplersPerSet}
    }};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 1; pci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    pci.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(dev, &pci, nullptr, &sceneDescPool_) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = sceneDescPool_; dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &sceneDescLayout_;
    if (vkAllocateDescriptorSets(dev, &dsai, &sceneDescSet_) != VK_SUCCESS) {
        std::fprintf(stderr, "[EditorVk] Failed to allocate descriptor sets.\n");
        return false;
    }

    // UBO buffer (persistently mapped to avoid per-frame map/unmap)
    if (!createBuffer(deviceCtx_.physicalDevice(), dev, 64, // Mat4 = 64 bytes
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      uboBuffer_, uboMemory_))
        return false;
    vkMapMemory(dev, uboMemory_, 0, 64, 0, &uboMapped_);

    VkDescriptorBufferInfo binfo{}; binfo.buffer = uboBuffer_; binfo.range = 64;

    constexpr VkDeviceSize kLightUboSize = sizeof(SceneLightsUbo);
    if (!createBuffer(deviceCtx_.physicalDevice(), dev, kLightUboSize,
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      lightUboBuffer_, lightUboMemory_))
        return false;
    vkMapMemory(dev, lightUboMemory_, 0, kLightUboSize, 0, &lightUboMapped_);
    if (lightUboMapped_) std::memset(lightUboMapped_, 0, kLightUboSize);

    VkDescriptorBufferInfo lightBinfo{};
    lightBinfo.buffer = lightUboBuffer_; lightBinfo.range = kLightUboSize;

    // Create a 1x1 white dummy texture for binding 1 (required by descriptor layout)
    VkPhysicalDevice pd = deviceCtx_.physicalDevice();
    if (!createImage(pd, dev, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                     dummyTexImage_, dummyTexMemory_))
        return false;
    dummyTexView_ = createView(dev, dummyTexImage_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    if (!dummyTexView_) return false;

    // Transition dummy image to SHADER_READ_ONLY_OPTIMAL
    {
        VkCommandBuffer tmpCmd = frameGraph_.commandBuffer(0);
        VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(tmpCmd, &cbi);

        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = dummyTexImage_;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(tmpCmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(tmpCmd);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1; si.pCommandBuffers = &tmpCmd;
        vkQueueSubmit(deviceCtx_.graphicsQueue(), 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(deviceCtx_.graphicsQueue());
        vkResetCommandBuffer(tmpCmd, 0);
    }

    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler = vpSampler_;
    imgInfo.imageView = dummyTexView_;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo terrainAlbedoInfo{};
    terrainAlbedoInfo.sampler = terrainTextures_.albedo.valid() ? terrainTextures_.albedo.sampler : vpSampler_;
    terrainAlbedoInfo.imageView = terrainTextures_.albedo.valid() ? terrainTextures_.albedo.view : dummyTexView_;
    terrainAlbedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // The depth pass runs before the viewport pass of every frame, so by the
    // time a fragment samples this the array is already read-only.
    VkDescriptorImageInfo shadowInfo{};
    shadowInfo.sampler = shadowMap_.sampler();
    shadowInfo.imageView = shadowMap_.imageView();
    shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo terrainNormalInfo{};
    terrainNormalInfo.sampler = terrainTextures_.normal.valid() ? terrainTextures_.normal.sampler : vpSampler_;
    terrainNormalInfo.imageView = terrainTextures_.normal.valid() ? terrainTextures_.normal.view : dummyTexView_;
    terrainNormalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 7> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = sceneDescSet_; writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1; writes[0].pBufferInfo = &binfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = sceneDescSet_; writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1; writes[1].pImageInfo = &imgInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = sceneDescSet_; writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[2].descriptorCount = 1; writes[2].pBufferInfo = &lightBinfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = sceneDescSet_; writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1; writes[3].pImageInfo = &shadowInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = sceneDescSet_; writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1; writes[4].pImageInfo = &terrainAlbedoInfo;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = sceneDescSet_; writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[5].descriptorCount = 1; writes[5].pImageInfo = &imgInfo;

    writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet = sceneDescSet_; writes[6].dstBinding = 6;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[6].descriptorCount = 1; writes[6].pImageInfo = &terrainNormalInfo;

    // Without a depth target binding 3 is absent, so its write is skipped by
    // sliding the remaining writes down one slot.
    if (!shadowMap_.valid()) {
        writes[3] = writes[4];
        writes[4] = writes[5];
        writes[5] = writes[6];
    }

    vkUpdateDescriptorSets(dev, shadowMap_.valid() ? 7u : 6u, writes.data(), 0, nullptr);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipelines
// ─────────────────────────────────────────────────────────────────────────────
bool EditorVkContext::createPipelines()
{
    VkDevice dev = deviceCtx_.device();
    VkExtent2D ext = {1280, 720}; // initial; viewport rendering uses dynamic viewport

    std::string shaderDir = VULKAN_SHADER_DIR;
    std::string err;

    // With a live depth target the layout also carries binding 3, which only the
    // "_shadow" fragment variants declare.
    const bool shadows = shadowMap_.valid();

    // The scene is shaded into the HDR attachment; vpRenderPass_ only resolves it.
    const VkRenderPass scenePass = hdr_.renderPass();

    if (!PipelineBuilder::createTerrainPipeline(dev, ext, scenePass, sceneDescLayout_,
            shaderDir + "/terrain.vert.spv",
            shaderDir + (shadows ? "/terrain_shadow.frag.spv" : "/terrain.frag.spv"),
            terrainPipelineLayout_, terrainPipeline_, err)) {
        std::fprintf(stderr, "[EditorVk] Terrain pipeline: %s\n", err.c_str());
        return false;
    }

    if (!PipelineBuilder::createWaterPipeline(dev, ext, scenePass, sceneDescLayout_,
            shaderDir + "/water.vert.spv", shaderDir + "/water.frag.spv",
            waterPipelineLayout_, waterPipeline_, err)) {
        std::fprintf(stderr, "[EditorVk] Water pipeline: %s\n", err.c_str());
        return false;
    }

    if (!PipelineBuilder::createBasicPipeline(dev, ext, scenePass, sceneDescLayout_,
            shaderDir + "/basic.vert.spv", shaderDir + "/basic.frag.spv",
            basicPipelineLayout_, basicPipeline_, err)) {
        std::fprintf(stderr, "[EditorVk] Basic pipeline: %s\n", err.c_str());
        return false;
    }

    // Selection outline: same unlit shaders as basicPipeline_, but front-face
    // culled so drawing an enlarged copy of the mesh only rasterizes what the
    // regular (smaller) draw doesn't already cover in the depth buffer - the
    // silhouette rim. Optional: without it, selection just has no GPU outline.
    if (!PipelineBuilder::createBasicPipeline(dev, ext, scenePass, sceneDescLayout_,
            shaderDir + "/basic.vert.spv", shaderDir + "/basic.frag.spv",
            outlinePipelineLayout_, outlinePipeline_, err, VK_CULL_MODE_FRONT_BIT)) {
        std::fprintf(stderr, "[EditorVk] Outline pipeline: %s (selection outline disabled)\n", err.c_str());
        outlinePipeline_ = VK_NULL_HANDLE;
        outlinePipelineLayout_ = VK_NULL_HANDLE;
    }

    // Lit variant is optional: without it the viewport keeps the flat shading.
    if (!PipelineBuilder::createBasicPipeline(dev, ext, scenePass, sceneDescLayout_,
            shaderDir + "/basic.vert.spv",
            shaderDir + (shadows ? "/basic_shadow.frag.spv" : "/basic_lit.frag.spv"),
            basicLitPipelineLayout_, basicLitPipeline_, err)) {
        std::fprintf(stderr, "[EditorVk] Lit basic pipeline: %s (scene lights disabled)\n", err.c_str());
        basicLitPipeline_ = VK_NULL_HANDLE;
        basicLitPipelineLayout_ = VK_NULL_HANDLE;
    }

    // Billboards are optional: without them the viewport still draws meshes.
    if (!PipelineBuilder::createBillboardPipeline(dev, ext, scenePass, sceneDescLayout_,
            shaderDir + "/billboard.vert.spv", shaderDir + "/billboard.frag.spv",
            billboardPipelineLayout_, billboardPipeline_, err)) {
        std::fprintf(stderr, "[EditorVk] Billboard pipeline: %s (billboards disabled)\n", err.c_str());
        billboardPipeline_ = VK_NULL_HANDLE;
        billboardPipelineLayout_ = VK_NULL_HANDLE;
    }

    // Skinning is optional: without it animated meshes just draw in bind pose.
    if (boneSetLayout_ != VK_NULL_HANDLE) {
        if (!PipelineBuilder::createSkinnedPipeline(dev, ext, scenePass,
                sceneDescLayout_, boneSetLayout_,
                shaderDir + "/skinned.vert.spv",
                shaderDir + (shadows ? "/skinned_shadow.frag.spv" : "/skinned_lit.frag.spv"),
                skinnedPipelineLayout_, skinnedPipeline_, err)) {
            std::fprintf(stderr, "[EditorVk] Skinned pipeline: %s (bind pose only)\n", err.c_str());
            skinnedPipeline_ = VK_NULL_HANDLE;
            skinnedPipelineLayout_ = VK_NULL_HANDLE;
        }
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bone palette — one dynamic-offset UBO shared by every skinned draw. Slots are
// padded to the device alignment, and the buffer carries one disjoint region per
// frame in flight so a frame being recorded never overwrites poses the GPU is
// still reading. Mirrors Renderer::createBoneResources so both paths bind the
// same set 1 and partition the same way.
// ─────────────────────────────────────────────────────────────────────────────
bool EditorVkContext::createBoneResources()
{
    VkDevice dev = deviceCtx_.device();

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(deviceCtx_.physicalDevice(), &props);
    const VkDeviceSize align = std::max<VkDeviceSize>(
        props.limits.minUniformBufferOffsetAlignment, 1);

    constexpr uint32_t kSlotsPerFrame = 64;
    const uint32_t regions = std::max<uint32_t>(swapchain_.imageCount(), 1);
    const uint32_t stride = static_cast<uint32_t>(
        ((dash::anim::kBonePaletteBytes + align - 1) / align) * align);

    if (!createBuffer(deviceCtx_.physicalDevice(), dev,
                      static_cast<VkDeviceSize>(stride) * kSlotsPerFrame * regions,
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      boneBuffer_, boneMemory_)) {
        std::fprintf(stderr, "[EditorVk] Failed to create bone palette buffer.\n");
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(dev, boneMemory_, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
        std::fprintf(stderr, "[EditorVk] Failed to map bone palette buffer.\n");
        return false;
    }
    bonePalette_.mapped = static_cast<unsigned char*>(mapped);
    bonePalette_.slotStride = stride;
    bonePalette_.regionSlots = kSlotsPerFrame;
    bonePalette_.regionCount = regions;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = 1; lci.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(dev, &lci, nullptr, &boneSetLayout_) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 1; pci.poolSizeCount = 1; pci.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(dev, &pci, nullptr, &boneDescPool_) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = boneDescPool_; dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &boneSetLayout_;
    if (vkAllocateDescriptorSets(dev, &dsai, &boneSet_) != VK_SUCCESS)
        return false;

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = boneBuffer_;
    bufInfo.offset = 0;
    bufInfo.range = dash::anim::kBonePaletteBytes;

    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = boneSet_;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufInfo;
    vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

    std::fprintf(stdout, "[EditorVk][Skin] Bone palette ready: %u frame region(s) x %u slots"
                 " of %u bytes (%u bones max, %llu bytes total).\n",
                 regions, kSlotsPerFrame, stride, dash::anim::kBonePaletteMatrixCount,
                 static_cast<unsigned long long>(bonePalette_.bufferBytes()));
    for (uint32_t r = 0; r < regions; ++r) {
        std::fprintf(stdout, "[EditorVk][Skin]   frame region %u: bytes [%u, %u)\n",
                     r, r * bonePalette_.regionStride(),
                     (r + 1) * bonePalette_.regionStride());
    }
    return true;
}

void EditorVkContext::destroyBoneResources()
{
    VkDevice dev = deviceCtx_.device();
    if (dev == VK_NULL_HANDLE) return;

    PipelineBuilder::destroy(dev, skinnedPipelineLayout_, skinnedPipeline_);
    skinnedPipelineLayout_ = VK_NULL_HANDLE;
    skinnedPipeline_ = VK_NULL_HANDLE;

    if (boneDescPool_) {
        vkDestroyDescriptorPool(dev, boneDescPool_, nullptr);
        boneDescPool_ = VK_NULL_HANDLE;
        boneSet_ = VK_NULL_HANDLE;
    }
    if (boneSetLayout_) {
        vkDestroyDescriptorSetLayout(dev, boneSetLayout_, nullptr);
        boneSetLayout_ = VK_NULL_HANDLE;
    }
    if (bonePalette_.mapped != nullptr) {
        vkUnmapMemory(dev, boneMemory_);
        bonePalette_ = dash::anim::BonePalette{};
    }
    if (boneBuffer_) { vkDestroyBuffer(dev, boneBuffer_, nullptr); boneBuffer_ = VK_NULL_HANDLE; }
    if (boneMemory_) { vkFreeMemory(dev, boneMemory_, nullptr);    boneMemory_ = VK_NULL_HANDLE; }
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh resolution — mirrors Renderer::resolveMesh so the viewport and the
// runtime pick the same model for a given RenderComponent::mesh.
// ─────────────────────────────────────────────────────────────────────────────
std::string EditorVkContext::resolveModelPath(const std::string& meshId) const
{
    if (meshId.empty() || meshId == "cube") return {};
    // Procedural ids are generated, never loaded, so they have no file on disk.
    if (dash::procmesh::isProceduralMeshId(meshId)) return {};

    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> candidates;
    const fs::path raw(meshId);
    if (raw.is_absolute()) {
        candidates.push_back(raw);
    } else {
#ifdef VULKAN_MODEL_DIR
        candidates.push_back(fs::path(VULKAN_MODEL_DIR) / raw);
#endif
        candidates.push_back(raw);
    }

    for (const auto& c : candidates) {
        if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) return c.string();
        ec.clear();
    }
    return {};
}

const dash::vkexp::MeshBuffers* EditorVkContext::resolveMesh(const std::string& meshId)
{
    if (meshId.empty() || meshId == "cube") return nullptr;

    if (CachedModel* cached = meshCache_.get(meshId)) {
        return cached->meshBuffers.indexCount() > 0 ? &cached->meshBuffers : nullptr;
    }

    if (dash::procmesh::isProceduralMeshId(meshId)) {
        CachedModel generated;
        if (!dash::procmesh::uploadProceduralMesh(meshId, "(editor)",
                                                  deviceCtx_.physicalDevice(),
                                                  deviceCtx_.device(),
                                                  generated.meshBuffers)) {
            generated.meshBuffers.shutdown(deviceCtx_.device());
        }
        CachedModel& proc = meshCache_.store(meshId, std::move(generated));
        return proc.meshBuffers.indexCount() > 0 ? &proc.meshBuffers : nullptr;
    }

    const std::string resolved = resolveModelPath(meshId);

    CachedModel model;
    if (resolved.empty()) {
        std::fprintf(stderr, "[EditorVk] Mesh not found: '%s' (using builtin cube)\n", meshId.c_str());
    } else {
        // .dashmesh carries the skinning stream; anything else goes through Assimp.
        const bool isDashMesh = std::filesystem::path(resolved).extension() == ".dashmesh";
        const bool loaded = isDashMesh
            ? model.meshBuffers.initFromDashMesh(deviceCtx_.physicalDevice(),
                                                 deviceCtx_.device(), resolved)
            : model.meshBuffers.initFromGLTF(deviceCtx_.physicalDevice(),
                                             deviceCtx_.device(), resolved);
        if (!loaded) {
            std::fprintf(stderr, "[EditorVk] Failed to load mesh: %s\n", resolved.c_str());
            model.meshBuffers.shutdown(deviceCtx_.device());
        }
    }

    // Failures are cached too so a missing model is not retried every frame.
    CachedModel& stored = meshCache_.store(meshId, std::move(model));
    return stored.meshBuffers.indexCount() > 0 ? &stored.meshBuffers : nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Offscreen render target
// ─────────────────────────────────────────────────────────────────────────────
bool EditorVkContext::createOffscreenTarget(uint32_t w, uint32_t h)
{
    VkDevice dev = deviceCtx_.device();
    VkPhysicalDevice pd = deviceCtx_.physicalDevice();

    // Color image
    if (!createImage(pd, dev, w, h, VK_FORMAT_B8G8R8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            vpColorImage_, vpColorMemory_))
        return false;
    vpColorView_ = createView(dev, vpColorImage_, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    // Scene colour and depth live in the HDR target; this one only receives the
    // resolved image, so it needs no depth attachment.
    if (!hdr_.createResources(pd, dev, w, h)) return false;

    // Sized with the viewport, so binding 5 has to be rewritten after a resize.
    // The prepass pipelines bake in that size and destroyResources() drops them,
    // so they are rebuilt here rather than once at init.
    if (ssao_.createResources(pd, dev, w, h) && sceneDescSet_ != VK_NULL_HANDLE) {
        ssao_.createPipelines(dev, VULKAN_SHADER_DIR, sceneDescLayout_, boneSetLayout_);

        VkDescriptorImageInfo ssaoInfo{};
        ssaoInfo.sampler = ssao_.sampler();
        ssaoInfo.imageView = ssao_.aoView();
        ssaoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = sceneDescSet_; write.dstBinding = 5;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1; write.pImageInfo = &ssaoInfo;
        vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
    }

    std::array<VkImageView, 1> atts = {vpColorView_};
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = vpRenderPass_;
    fci.attachmentCount = 1; fci.pAttachments = atts.data();
    fci.width = w; fci.height = h; fci.layers = 1;
    if (vkCreateFramebuffer(dev, &fci, nullptr, &vpFramebuffer_) != VK_SUCCESS)
        return false;

    vpWidth_ = w; vpHeight_ = h;

    // Register with ImGui
    vpImGuiDesc_ = ImGui_ImplVulkan_AddTexture(vpSampler_, vpColorView_,
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    return true;
}

void EditorVkContext::destroyOffscreenTarget()
{
    VkDevice dev = deviceCtx_.device();
    if (dev == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(dev);

    if (vpImGuiDesc_ != VK_NULL_HANDLE) {
        // Only valid while the ImGui Vulkan backend is alive. On shutdown the
        // backend is torn down first, which already frees this descriptor set.
        if (ImGui::GetCurrentContext() != nullptr &&
            ImGui::GetIO().BackendRendererUserData != nullptr) {
            ImGui_ImplVulkan_RemoveTexture(vpImGuiDesc_);
        }
        vpImGuiDesc_ = VK_NULL_HANDLE;
    }
    if (vpFramebuffer_) { vkDestroyFramebuffer(dev, vpFramebuffer_, nullptr); vpFramebuffer_ = VK_NULL_HANDLE; }
    ssao_.destroyResources(dev);
    hdr_.destroyResources(dev);
    if (vpColorView_)   { vkDestroyImageView(dev, vpColorView_, nullptr);     vpColorView_ = VK_NULL_HANDLE; }
    if (vpColorImage_)  { vkDestroyImage(dev, vpColorImage_, nullptr);        vpColorImage_ = VK_NULL_HANDLE; }
    if (vpColorMemory_) { vkFreeMemory(dev, vpColorMemory_, nullptr);         vpColorMemory_ = VK_NULL_HANDLE; }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame lifecycle
// ─────────────────────────────────────────────────────────────────────────────
// TEMP-VERIFY: dumps the offscreen viewport to PNG so the shadows can be
// eyeballed without screen-recording permission. Removed after verification.
static void tempDumpViewport(VkPhysicalDevice pd, VkDevice dev, VkQueue queue,
                             uint32_t queueFamily, VkImage image,
                             uint32_t w, uint32_t h, const char* path);

bool EditorVkContext::beginFrame()
{
    if (!frameGraph_.beginFrame(currentImageIndex_))
        return false;

    if (const char* dumpAt = std::getenv("DASH_DUMP_VIEWPORT")) {
        static int frame = 0;
        if (++frame == std::atoi(dumpAt) && vpColorImage_ != VK_NULL_HANDLE) {
            tempDumpViewport(deviceCtx_.physicalDevice(), deviceCtx_.device(),
                             deviceCtx_.graphicsQueue(),
                             deviceCtx_.queueFamilies().graphicsFamily.value(),
                             vpColorImage_, vpWidth_, vpHeight_,
                             "/tmp/editor_viewport.png");
        }
    }

    frameInFlight_ = true;

    VkCommandBuffer cmd = frameGraph_.commandBuffer(currentImageIndex_);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    return true;
}

VkCommandBuffer EditorVkContext::currentCmd()
{
    return frameGraph_.commandBuffer(currentImageIndex_);
}

void EditorVkContext::endFrame()
{
    VkCommandBuffer cmd = currentCmd();

    // Begin swapchain render pass for ImGui
    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass = swapchain_.renderPass();
    rpbi.framebuffer = frameGraph_.framebuffer(currentImageIndex_);
    rpbi.renderArea.extent = swapchain_.extent();
    VkClearValue clears[2]{};
    clears[0].color = {{0.118f, 0.118f, 0.118f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    rpbi.clearValueCount = 2; rpbi.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    frameGraph_.endFrame(currentImageIndex_);
    frameInFlight_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Offscreen viewport rendering
// ─────────────────────────────────────────────────────────────────────────────
void EditorVkContext::ensureViewportSize(uint32_t width, uint32_t height)
{
    // Resize only when dimensions change by more than 4 pixels (avoids
    // per-frame vkDeviceWaitIdle + reallocation from sub-pixel jitter)
    if (width == 0 || height == 0) return;
    const int dw = static_cast<int>(width) - static_cast<int>(vpWidth_);
    const int dh = static_cast<int>(height) - static_cast<int>(vpHeight_);
    if (dw * dw + dh * dh > 16 || vpWidth_ == 0) {
        destroyOffscreenTarget();
        createOffscreenTarget(width, height);
    }
}

void EditorVkContext::beginViewportRender(uint32_t width, uint32_t height)
{
    ensureViewportSize(width, height);

    VkCommandBuffer cmd = currentCmd();

    const float clear[4] = {0.157f, 0.216f, 0.294f, 1.0f};  // dark sky blue
    hdr_.beginPass(cmd, clear);

    // Set viewport + scissor
    VkViewport vp{};
    vp.width = static_cast<float>(vpWidth_);
    vp.height = static_cast<float>(vpHeight_);
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{{0, 0}, {vpWidth_, vpHeight_}};
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

void EditorVkContext::endViewportRender()
{
    VkCommandBuffer cmd = currentCmd();
    hdr_.endPass(cmd);

    // ImGui samples vpColorImage_ raw onto a _UNORM swapchain, so this resolve
    // is the one that has to encode sRGB itself.
    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass = vpRenderPass_;
    rpbi.framebuffer = vpFramebuffer_;
    rpbi.renderArea.extent = {vpWidth_, vpHeight_};
    VkClearValue clear{};
    rpbi.clearValueCount = 1; rpbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    hdr_.drawTonemap(cmd, grading_, true);
    vkCmdEndRenderPass(cmd);
}

ImTextureID EditorVkContext::viewportTexture() const
{
    return reinterpret_cast<ImTextureID>(vpImGuiDesc_);
}

void EditorVkContext::recordParticles(const dash::vkexp::Mat4& viewProj,
                                      const dash::vkexp::Vec3& camRight, const dash::vkexp::Vec3& camUp,
                                      const std::vector<dash::vfx::ParticleInstance>& alphaBatch,
                                      const std::vector<dash::vfx::ParticleInstance>& additiveBatch)
{
    if (!particles_.valid()) return;
    particles_.record(currentCmd(), deviceCtx_.device(), currentFrameIndex(),
                      {vpWidth_, vpHeight_}, viewProj, camRight, camUp,
                      alphaBatch, additiveBatch);
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera UBO
// ─────────────────────────────────────────────────────────────────────────────
void EditorVkContext::updateCamera(const float viewProj[16])
{
    if (uboMapped_) {
        std::memcpy(uboMapped_, viewProj, 64);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene light UBO
// ─────────────────────────────────────────────────────────────────────────────
void EditorVkContext::updateSceneLights(const SceneLightsUbo& ubo, int count)
{
    if (!lightUboMapped_) return;

    const int clamped = std::max(0, std::min(count, kMaxSceneLights));
    const std::size_t bytes = offsetof(SceneLightsUbo, lights)
                            + static_cast<std::size_t>(clamped) * sizeof(SceneLightGpu);
    std::memcpy(lightUboMapped_, &ubo, bytes);
}

// ─────────────────────────────────────────────────────────────────────────────
// Directional shadow cascades — same math and tuning as Renderer, so the
// viewport and the game agree on where the shadows land.
// ─────────────────────────────────────────────────────────────────────────────
void EditorVkContext::updateShadowCascades(const Vec3& camPos, const Vec3& forward,
                                           const Vec3& right, const Vec3& up,
                                           float fovYRadians, float aspect,
                                           const Vec3& lightDir, int lightIndex)
{
    for (Mat4& m : shadowMatrices_) m = Mat4{};
    for (float& v : shadowSplits_) v = 0.0f;
    for (float& v : shadowTexels_) v = 0.0f;
    for (float& v : shadowDepthBias_) v = 0.0f;
    for (float& v : shadowParams_) v = 0.0f;
    shadowLightIndex_ = -1;

    if (!shadowMap_.valid() || lightIndex < 0) return;

    shadowLightIndex_ = lightIndex;
    shadowLightDir_ = lightDir;
    shadowParams_[0] = static_cast<float>(lightIndex + 1);
    shadowParams_[1] = 1.0f / static_cast<float>(ShadowMap::kResolution);
    shadowParams_[2] = 0.10f;

    float splits[kShadowCascades];
    computeCascadeSplits(0.5f, kShadowMaxDistance, 0.7f, splits);

    float sliceNear = 0.1f;
    for (int i = 0; i < kShadowCascades; ++i) {
        const ShadowVolume slice = frustumSliceVolume(camPos, forward, right, up,
                                                      fovYRadians, aspect,
                                                      sliceNear, splits[i]);
        const ShadowVolume snapped =
            snapVolumeToTexelGrid(lightDir, slice, ShadowMap::kResolution);
        shadowMatrices_[i] = directionalLightMatrix(lightDir, snapped);

        const float texel = shadowTexelWorldSize(snapped, ShadowMap::kResolution);
        shadowSplits_[i] = splits[i];
        shadowTexels_[i] = texel;
        shadowDepthBias_[i] = (texel * 1.5f) / (2.0f * snapped.radius);

        sliceNear = splits[i];
    }

    if (!shadowLogged_) {
        shadowLogged_ = true;
        std::printf("[Shadow] Viewport light %d casts over %d cascades to %.0f units"
                    " (%.0f / %.0f / %.0f mm per texel).\n",
                    lightIndex, kShadowCascades, kShadowMaxDistance,
                    shadowTexels_[0] * 1000.0f,
                    shadowTexels_[1] * 1000.0f,
                    shadowTexels_[2] * 1000.0f);
    }
}

void EditorVkContext::fillShadowUbo(SceneLightsUbo& ubo) const
{
    for (int i = 0; i < kShadowCascades; ++i) {
        std::memcpy(ubo.shadowMatrices[i], shadowMatrices_[i].m, sizeof(shadowMatrices_[i].m));
    }
    std::memcpy(ubo.shadowSplits, shadowSplits_, sizeof(ubo.shadowSplits));
    std::memcpy(ubo.shadowTexels, shadowTexels_, sizeof(ubo.shadowTexels));
    std::memcpy(ubo.shadowDepthBias, shadowDepthBias_, sizeof(ubo.shadowDepthBias));
    std::memcpy(ubo.shadowParams, shadowParams_, sizeof(ubo.shadowParams));
}

void EditorVkContext::recordShadowPass(const std::vector<RenderInstance>& instances,
                                       const std::vector<InstanceResources>& resources)
{
    if (!shadowMap_.valid()) return;

    VkCommandBuffer cmd = currentCmd();
    const bool draws = shadowLightIndex_ >= 0 && shadowMap_.hasPipelines();
    const bool terrainCasts = draws
                           && shadowMap_.terrainPipeline() != VK_NULL_HANDLE
                           && terrainMeshBuf_.indexCount() > 0;

    // Billboards only spin around Y to face the light, which anchors the shadow
    // at the foot of the sprite instead of detaching it.
    Vec3 lightRight{1.0f, 0.0f, 0.0f};
    {
        const Vec3 axis = normalize(cross(shadowLightDir_, Vec3{0.0f, 1.0f, 0.0f}));
        if (dot(axis, axis) > 0.5f) lightRight = axis;
    }

    for (int cascade = 0; cascade < kShadowCascades; ++cascade) {
        // Entered even with no caster: the clear is what leaves the layer in the
        // layout the descriptor written at init already promises.
        shadowMap_.beginPass(cmd, cascade);

        if (terrainCasts) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMap_.terrainPipeline());
            VkBuffer vb[] = { terrainMeshBuf_.vertexBuffer() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
            vkCmdBindIndexBuffer(cmd, terrainMeshBuf_.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            // Terrain vertices are already in world space.
            float terrainPC[kShadowPushConstantFloats];
            const Mat4 model = identity();
            std::memcpy(terrainPC, model.m, sizeof(model.m));
            std::memcpy(terrainPC + 16, shadowMatrices_[cascade].m,
                        sizeof(shadowMatrices_[cascade].m));
            vkCmdPushConstants(cmd, shadowMap_.terrainPipelineLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(terrainPC), terrainPC);

            vkCmdDrawIndexed(cmd, terrainMeshBuf_.indexCount(), 1, 0, 0, 0);
        }

        if (draws && cubeMeshBuf_.indexCount() > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMap_.pipeline());
            VkBuffer vb[] = { cubeMeshBuf_.vertexBuffer() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
            vkCmdBindIndexBuffer(cmd, cubeMeshBuf_.indexBuffer(), 0, cubeMeshBuf_.indexType());

            SceneDrawParams params;
            params.depthOnly         = true;
            params.opaquePipeline    = shadowMap_.pipeline();
            params.opaqueLayout      = shadowMap_.pipelineLayout();
            params.billboardPipeline = shadowMap_.billboardPipeline();
            params.billboardLayout   = shadowMap_.billboardPipelineLayout();
            params.skinnedPipeline   = shadowMap_.skinnedPipeline();
            params.skinnedLayout     = shadowMap_.skinnedPipelineLayout();
            params.defaultSet        = sceneDescSet_;
            params.fallbackMesh      = &cubeMeshBuf_;
            params.boneSet           = boneSet_;
            params.bonePalette       = &bonePalette_;
            // Culling then happens against this cascade's frustum.
            params.viewProj          = shadowMatrices_[cascade];
            params.cameraRight       = lightRight;
            params.cameraUp          = Vec3{0.0f, 1.0f, 0.0f};

            drawSceneInstances(cmd, instances, resources, LightingParams{}, params);
        }

        shadowMap_.endPass(cmd);
    }
}

void EditorVkContext::recordSsaoPass(const std::vector<RenderInstance>& instances,
                                     const std::vector<InstanceResources>& resources,
                                     const Mat4& viewProj, float aspect)
{
    if (!ssao_.valid()) return;

    VkCommandBuffer cmd = currentCmd();

    // Entered even with nothing to draw: the clear is what leaves the depth
    // image in the layout the descriptor already promises.
    ssao_.beginDepthPass(cmd);

    const bool draws = ssao_.hasPipelines();

    if (draws && ssao_.terrainPipeline() != VK_NULL_HANDLE && terrainMeshBuf_.indexCount() > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssao_.terrainPipeline());
        VkBuffer vb[] = { terrainMeshBuf_.vertexBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, terrainMeshBuf_.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        // Terrain vertices are already in world space.
        float terrainPC[kShadowPushConstantFloats];
        const Mat4 model = identity();
        std::memcpy(terrainPC, model.m, sizeof(model.m));
        std::memcpy(terrainPC + 16, viewProj.m, sizeof(viewProj.m));
        vkCmdPushConstants(cmd, ssao_.terrainPipelineLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(terrainPC), terrainPC);

        vkCmdDrawIndexed(cmd, terrainMeshBuf_.indexCount(), 1, 0, 0, 0);
    }

    if (draws && cubeMeshBuf_.indexCount() > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ssao_.depthPipeline());
        VkBuffer vb[] = { cubeMeshBuf_.vertexBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, cubeMeshBuf_.indexBuffer(), 0, cubeMeshBuf_.indexType());

        SceneDrawParams params;
        params.depthOnly         = true;
        params.opaquePipeline    = ssao_.depthPipeline();
        params.opaqueLayout      = ssao_.depthPipelineLayout();
        params.billboardPipeline = ssao_.billboardPipeline();
        params.billboardLayout   = ssao_.billboardPipelineLayout();
        params.skinnedPipeline   = ssao_.skinnedPipeline();
        params.skinnedLayout     = ssao_.skinnedPipelineLayout();
        params.defaultSet        = sceneDescSet_;
        params.fallbackMesh      = &cubeMeshBuf_;
        params.boneSet           = boneSet_;
        params.bonePalette       = &bonePalette_;
        params.viewProj          = viewProj;

        drawSceneInstances(cmd, instances, resources, LightingParams{}, params);
    }

    ssao_.endDepthPass(cmd);

    // Mirrors the projection the viewport camera builds, including the Y flip
    // Vulkan's clip space needs.
    constexpr float kFovY = 60.0f * 0.0174532925f;
    constexpr float kNear = 0.1f;
    constexpr float kFar = 500.0f;
    const float focal = 1.0f / std::tan(kFovY * 0.5f);
    ssao_.recordResolve(cmd, ssaoParams_, focal / aspect, -focal, kNear, kFar);
}

// ─────────────────────────────────────────────────────────────────────────────
// Terrain mesh update
// ─────────────────────────────────────────────────────────────────────────────
void EditorVkContext::updateTerrainMesh(TerrainMesh& terrain)
{
    if (!terrain.dirty()) return;

    VkDevice dev = deviceCtx_.device();
    VkPhysicalDevice pd = deviceCtx_.physicalDevice();

    vkDeviceWaitIdle(dev);

    // Rebuild terrain surface
    std::vector<TerrainVkVertex> verts;
    std::vector<uint32_t> indices;
    terrain.buildVulkanMesh(verts, indices);
    terrain.buildCliffWalls(verts, indices);

    terrainMeshBuf_.shutdown(dev);
    if (!verts.empty()) {
        terrainMeshBuf_.initFromData(pd, dev,
            verts.data(), static_cast<uint32_t>(verts.size() * sizeof(TerrainVkVertex)),
            indices.data(), static_cast<uint32_t>(indices.size() * sizeof(uint32_t)),
            static_cast<uint32_t>(indices.size()));
    }

    // Rebuild water mesh
    std::vector<TerrainVkVertex> wverts;
    std::vector<uint32_t> windices;
    terrain.buildWaterMesh(wverts, windices);

    waterMeshBuf_.shutdown(dev);
    if (!wverts.empty()) {
        waterMeshBuf_.initFromData(pd, dev,
            wverts.data(), static_cast<uint32_t>(wverts.size() * sizeof(TerrainVkVertex)),
            windices.data(), static_cast<uint32_t>(windices.size() * sizeof(uint32_t)),
            static_cast<uint32_t>(windices.size()));
    }

    terrain.clearDirty();
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────
void EditorVkContext::shutdown()
{
    VkDevice dev = deviceCtx_.device();
    if (dev) vkDeviceWaitIdle(dev);

    destroyOffscreenTarget();

    cubeMeshBuf_.shutdown(dev);
    terrainMeshBuf_.shutdown(dev);
    waterMeshBuf_.shutdown(dev);
    meshCache_.clear(dev);

    if (vpSampler_) { vkDestroySampler(dev, vpSampler_, nullptr); vpSampler_ = VK_NULL_HANDLE; }
    if (vpRenderPass_) { vkDestroyRenderPass(dev, vpRenderPass_, nullptr); vpRenderPass_ = VK_NULL_HANDLE; }

    shadowMap_.shutdown(dev);
    ssao_.shutdown(dev);
    particles_.shutdown(dev);

    destroyBoneResources();

    // Terrain texture arrays
    destroyTerrainTextureSet(dev, terrainTextures_);

    PipelineBuilder::destroy(dev, terrainPipelineLayout_, terrainPipeline_);
    PipelineBuilder::destroy(dev, waterPipelineLayout_, waterPipeline_);
    PipelineBuilder::destroy(dev, basicPipelineLayout_, basicPipeline_);
    PipelineBuilder::destroy(dev, basicLitPipelineLayout_, basicLitPipeline_);
    PipelineBuilder::destroy(dev, outlinePipelineLayout_, outlinePipeline_);
    terrainPipelineLayout_ = VK_NULL_HANDLE; terrainPipeline_ = VK_NULL_HANDLE;
    waterPipelineLayout_ = VK_NULL_HANDLE;   waterPipeline_ = VK_NULL_HANDLE;
    basicPipelineLayout_ = VK_NULL_HANDLE;   basicPipeline_ = VK_NULL_HANDLE;
    basicLitPipelineLayout_ = VK_NULL_HANDLE; basicLitPipeline_ = VK_NULL_HANDLE;
    outlinePipelineLayout_ = VK_NULL_HANDLE; outlinePipeline_ = VK_NULL_HANDLE;

    if (uboBuffer_) { vkDestroyBuffer(dev, uboBuffer_, nullptr); uboBuffer_ = VK_NULL_HANDLE; }
    if (uboMemory_) {
        if (uboMapped_) { vkUnmapMemory(dev, uboMemory_); uboMapped_ = nullptr; }
        vkFreeMemory(dev, uboMemory_, nullptr); uboMemory_ = VK_NULL_HANDLE;
    }
    if (lightUboBuffer_) { vkDestroyBuffer(dev, lightUboBuffer_, nullptr); lightUboBuffer_ = VK_NULL_HANDLE; }
    if (lightUboMemory_) {
        if (lightUboMapped_) { vkUnmapMemory(dev, lightUboMemory_); lightUboMapped_ = nullptr; }
        vkFreeMemory(dev, lightUboMemory_, nullptr); lightUboMemory_ = VK_NULL_HANDLE;
    }
    if (dummyTexView_) { vkDestroyImageView(dev, dummyTexView_, nullptr); dummyTexView_ = VK_NULL_HANDLE; }
    if (dummyTexImage_) { vkDestroyImage(dev, dummyTexImage_, nullptr); dummyTexImage_ = VK_NULL_HANDLE; }
    if (dummyTexMemory_) { vkFreeMemory(dev, dummyTexMemory_, nullptr); dummyTexMemory_ = VK_NULL_HANDLE; }
    if (sceneDescPool_) { vkDestroyDescriptorPool(dev, sceneDescPool_, nullptr); sceneDescPool_ = VK_NULL_HANDLE; }
    if (sceneDescLayout_) { vkDestroyDescriptorSetLayout(dev, sceneDescLayout_, nullptr); sceneDescLayout_ = VK_NULL_HANDLE; }

    frameGraph_.shutdown();
    swapchain_.shutdown(dev);
    deviceCtx_.shutdown();

    if (surface_) { vkDestroySurfaceKHR(instance_, surface_, nullptr); surface_ = VK_NULL_HANDLE; }
    if (instance_) { vkDestroyInstance(instance_, nullptr); instance_ = VK_NULL_HANDLE; }
}
