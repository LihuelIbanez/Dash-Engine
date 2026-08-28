#include "EditorVkContext.h"
#include "rendering/vulkan/PipelineBuilder.h"
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

    // ── Scene descriptors (UBO for camera) ──────────────────────────────────
    if (!createSceneDescriptors()) return false;

    // ── Offscreen viewport render pass ──────────────────────────────────────
    {
        VkAttachmentDescription colorAtt{};
        colorAtt.format = VK_FORMAT_B8G8R8A8_UNORM;
        colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkAttachmentDescription depthAtt{};
        depthAtt.format = VK_FORMAT_D32_SFLOAT;
        depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &colorRef;
        sub.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 2> atts = {colorAtt, depthAtt};
        VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpci.attachmentCount = 2; rpci.pAttachments = atts.data();
        rpci.subpassCount = 1; rpci.pSubpasses = &sub;
        rpci.dependencyCount = 1; rpci.pDependencies = &dep;
        if (vkCreateRenderPass(deviceCtx_.device(), &rpci, nullptr, &vpRenderPass_) != VK_SUCCESS)
            return false;
    }

    // ── Pipelines ───────────────────────────────────────────────────────────
    if (!createPipelines()) return false;

    // ── Cube mesh (for entity rendering) ────────────────────────────────────
    cubeMeshBuf_.initCube(deviceCtx_.physicalDevice(), deviceCtx_.device());

    // ── Wolf GLTF mesh (for enemy entities) ────────────────────────────────
#ifdef VULKAN_MODEL_DIR
    {
        std::string wolfPath = std::string(VULKAN_MODEL_DIR) + "/Wolf-Blender-2.82a.gltf";
        if (!wolfMeshBuf_.initFromGLTF(deviceCtx_.physicalDevice(), deviceCtx_.device(), wolfPath)) {
            std::fprintf(stderr, "[EditorVkContext] Warning: could not load wolf model, enemies will use cube fallback\n");
        }
    }
#endif

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
// scene lights UBO at binding 2, terrain arrays at bindings 4/5)
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

    // 3 is reserved for the runtime shadow cascades, which the viewport lacks.
    VkDescriptorSetLayoutBinding terrainAlbedoB{};
    terrainAlbedoB.binding = 4; terrainAlbedoB.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    terrainAlbedoB.descriptorCount = 1; terrainAlbedoB.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 4> bindings = {uboB, samplerB, lightsB, terrainAlbedoB};
    VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = static_cast<uint32_t>(bindings.size()); lci.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(dev, &lci, nullptr, &sceneDescLayout_) != VK_SUCCESS)
        return false;

    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}
    }};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = 1; pci.poolSizeCount = 2; pci.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(dev, &pci, nullptr, &sceneDescPool_) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = sceneDescPool_; dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &sceneDescLayout_;
    if (vkAllocateDescriptorSets(dev, &dsai, &sceneDescSet_) != VK_SUCCESS)
        return false;

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

    std::array<VkWriteDescriptorSet, 4> writes{};
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
    writes[3].dstSet = sceneDescSet_; writes[3].dstBinding = 4;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1; writes[3].pImageInfo = &terrainAlbedoInfo;

    vkUpdateDescriptorSets(dev, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

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

    if (!PipelineBuilder::createTerrainPipeline(dev, ext, vpRenderPass_, sceneDescLayout_,
            shaderDir + "/terrain.vert.spv", shaderDir + "/terrain.frag.spv",
            terrainPipelineLayout_, terrainPipeline_, err)) {
        std::fprintf(stderr, "[EditorVk] Terrain pipeline: %s\n", err.c_str());
        return false;
    }

    if (!PipelineBuilder::createWaterPipeline(dev, ext, vpRenderPass_, sceneDescLayout_,
            shaderDir + "/water.vert.spv", shaderDir + "/water.frag.spv",
            waterPipelineLayout_, waterPipeline_, err)) {
        std::fprintf(stderr, "[EditorVk] Water pipeline: %s\n", err.c_str());
        return false;
    }

    if (!PipelineBuilder::createBasicPipeline(dev, ext, vpRenderPass_, sceneDescLayout_,
            shaderDir + "/basic.vert.spv", shaderDir + "/basic.frag.spv",
            basicPipelineLayout_, basicPipeline_, err)) {
        std::fprintf(stderr, "[EditorVk] Basic pipeline: %s\n", err.c_str());
        return false;
    }

    // Lit variant is optional: without it the viewport keeps the flat shading.
    if (!PipelineBuilder::createBasicPipeline(dev, ext, vpRenderPass_, sceneDescLayout_,
            shaderDir + "/basic.vert.spv", shaderDir + "/basic_lit.frag.spv",
            basicLitPipelineLayout_, basicLitPipeline_, err)) {
        std::fprintf(stderr, "[EditorVk] Lit basic pipeline: %s (scene lights disabled)\n", err.c_str());
        basicLitPipeline_ = VK_NULL_HANDLE;
        basicLitPipelineLayout_ = VK_NULL_HANDLE;
    }

    // Billboards are optional: without them the viewport still draws meshes.
    if (!PipelineBuilder::createBillboardPipeline(dev, ext, vpRenderPass_, sceneDescLayout_,
            shaderDir + "/billboard.vert.spv", shaderDir + "/billboard.frag.spv",
            billboardPipelineLayout_, billboardPipeline_, err)) {
        std::fprintf(stderr, "[EditorVk] Billboard pipeline: %s (billboards disabled)\n", err.c_str());
        billboardPipeline_ = VK_NULL_HANDLE;
        billboardPipelineLayout_ = VK_NULL_HANDLE;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mesh resolution — mirrors Renderer::resolveMesh so the viewport and the
// runtime pick the same model for a given RenderComponent::mesh.
// ─────────────────────────────────────────────────────────────────────────────
const dash::vkexp::MeshBuffers* EditorVkContext::resolveMesh(const std::string& meshId)
{
    if (meshId.empty() || meshId == "cube") return nullptr;

    if (CachedModel* cached = meshCache_.get(meshId)) {
        return cached->meshBuffers.indexCount() > 0 ? &cached->meshBuffers : nullptr;
    }

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

    fs::path resolved;
    for (const auto& c : candidates) {
        if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) { resolved = c; break; }
        ec.clear();
    }

    CachedModel model;
    if (resolved.empty()) {
        std::fprintf(stderr, "[EditorVk] Mesh not found: '%s' (using builtin cube)\n", meshId.c_str());
    } else if (!model.meshBuffers.initFromGLTF(deviceCtx_.physicalDevice(),
                                               deviceCtx_.device(),
                                               resolved.string())) {
        std::fprintf(stderr, "[EditorVk] Failed to load mesh: %s\n", resolved.string().c_str());
        model.meshBuffers.shutdown(deviceCtx_.device());
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
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            vpColorImage_, vpColorMemory_))
        return false;
    vpColorView_ = createView(dev, vpColorImage_, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    // Depth image
    if (!createImage(pd, dev, w, h, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            vpDepthImage_, vpDepthMemory_))
        return false;
    vpDepthView_ = createView(dev, vpDepthImage_, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);

    // Framebuffer
    std::array<VkImageView, 2> atts = {vpColorView_, vpDepthView_};
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = vpRenderPass_;
    fci.attachmentCount = 2; fci.pAttachments = atts.data();
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
    if (vpDepthView_)   { vkDestroyImageView(dev, vpDepthView_, nullptr);     vpDepthView_ = VK_NULL_HANDLE; }
    if (vpDepthImage_)  { vkDestroyImage(dev, vpDepthImage_, nullptr);        vpDepthImage_ = VK_NULL_HANDLE; }
    if (vpDepthMemory_) { vkFreeMemory(dev, vpDepthMemory_, nullptr);         vpDepthMemory_ = VK_NULL_HANDLE; }
    if (vpColorView_)   { vkDestroyImageView(dev, vpColorView_, nullptr);     vpColorView_ = VK_NULL_HANDLE; }
    if (vpColorImage_)  { vkDestroyImage(dev, vpColorImage_, nullptr);        vpColorImage_ = VK_NULL_HANDLE; }
    if (vpColorMemory_) { vkFreeMemory(dev, vpColorMemory_, nullptr);         vpColorMemory_ = VK_NULL_HANDLE; }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame lifecycle
// ─────────────────────────────────────────────────────────────────────────────
bool EditorVkContext::beginFrame()
{
    if (!frameGraph_.beginFrame(currentImageIndex_))
        return false;
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
void EditorVkContext::beginViewportRender(uint32_t width, uint32_t height)
{
    // Resize only when dimensions change by more than 4 pixels (avoids
    // per-frame vkDeviceWaitIdle + reallocation from sub-pixel jitter)
    if (width > 0 && height > 0) {
        int dw = static_cast<int>(width) - static_cast<int>(vpWidth_);
        int dh = static_cast<int>(height) - static_cast<int>(vpHeight_);
        if (dw*dw + dh*dh > 16 || vpWidth_ == 0) { // >4px change or first time
            destroyOffscreenTarget();
            createOffscreenTarget(width, height);
        }
    }

    VkCommandBuffer cmd = currentCmd();

    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass = vpRenderPass_;
    rpbi.framebuffer = vpFramebuffer_;
    rpbi.renderArea.extent = {vpWidth_, vpHeight_};
    VkClearValue clears[2]{};
    clears[0].color = {{0.157f, 0.216f, 0.294f, 1.0f}}; // dark sky blue
    clears[1].depthStencil = {1.0f, 0};
    rpbi.clearValueCount = 2; rpbi.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

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
    vkCmdEndRenderPass(cmd);
    // The render pass finalLayout transitions color to SHADER_READ_ONLY_OPTIMAL automatically
}

ImTextureID EditorVkContext::viewportTexture() const
{
    return reinterpret_cast<ImTextureID>(vpImGuiDesc_);
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
    wolfMeshBuf_.shutdown(dev);
    terrainMeshBuf_.shutdown(dev);
    waterMeshBuf_.shutdown(dev);
    meshCache_.clear(dev);

    if (vpSampler_) { vkDestroySampler(dev, vpSampler_, nullptr); vpSampler_ = VK_NULL_HANDLE; }
    if (vpRenderPass_) { vkDestroyRenderPass(dev, vpRenderPass_, nullptr); vpRenderPass_ = VK_NULL_HANDLE; }

    // Terrain texture arrays
    destroyTerrainTextureSet(dev, terrainTextures_);

    PipelineBuilder::destroy(dev, terrainPipelineLayout_, terrainPipeline_);
    PipelineBuilder::destroy(dev, waterPipelineLayout_, waterPipeline_);
    PipelineBuilder::destroy(dev, basicPipelineLayout_, basicPipeline_);
    PipelineBuilder::destroy(dev, basicLitPipelineLayout_, basicLitPipeline_);
    terrainPipelineLayout_ = VK_NULL_HANDLE; terrainPipeline_ = VK_NULL_HANDLE;
    waterPipelineLayout_ = VK_NULL_HANDLE;   waterPipeline_ = VK_NULL_HANDLE;
    basicPipelineLayout_ = VK_NULL_HANDLE;   basicPipeline_ = VK_NULL_HANDLE;
    basicLitPipelineLayout_ = VK_NULL_HANDLE; basicLitPipeline_ = VK_NULL_HANDLE;

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
