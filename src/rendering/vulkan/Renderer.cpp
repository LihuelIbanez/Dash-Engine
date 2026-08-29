#include "rendering/vulkan/Renderer.h"
#include "rendering/vulkan/VkMath.h"
#include "rendering/vulkan/SceneLoader.h"
#include "rendering/vulkan/CameraController.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <set>
#include <unordered_map>

#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

#include "game/physics/DebugPhysicsDraw.h"
#include "rendering/Frustum.h"
#include "rendering/vulkan/PipelineBuilder.h"
#include "rendering/vulkan/SceneRenderer.h"
#include "rendering/mesh/TerrainVertex.h"
#include "world/TerrainMesh.h"

#ifndef VULKAN_SHADER_DIR
#define VULKAN_SHADER_DIR ""
#endif

#ifndef VULKAN_ASSETS_DIR
#define VULKAN_ASSETS_DIR ""
#endif

namespace dash::vkexp {

using json = nlohmann::json;

namespace {

static constexpr const char* kGetPhysicalDeviceProps2Ext = "VK_KHR_get_physical_device_properties2";

// Distinguishes a material GUID from a path reference: 8-4-4-4-12 hex, the
// shape AssetDatabase::generateGuid() produces.
bool looksLikeGuid(const std::string& s)
{
    if (s.size() != 36) return false;
    for (size_t i = 0; i < s.size(); ++i) {
        const bool dashPos = (i == 8 || i == 13 || i == 18 || i == 23);
        if (dashPos) {
            if (s[i] != '-') return false;
        } else if (!std::isxdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

static bool hasValidationLayer()
{
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    if (layerCount == 0) return false;

    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
    for (const auto& layer : layers) {
        if (std::string(layer.layerName) == "VK_LAYER_KHRONOS_validation") return true;
    }
    return false;
}

} // namespace

Renderer::~Renderer()
{
    shutdown();
}

void Renderer::setScenePath(const std::string& scenePath)
{
    scenePath_ = scenePath;
}

void Renderer::setEditorStatePath(const std::string& statePath)
{
    editorBridge_.setStatePath(statePath);
}

void Renderer::setEmbeddedPreview(bool enabled)
{
    editorBridge_.setEmbeddedPreview(enabled);
}

bool Renderer::createInstance(const std::vector<const char*>& requiredExtensions)
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Dash Vulkan Bootstrap";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "Dash-Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    std::set<std::string> extensionsSet;
    for (const char* ext : requiredExtensions) {
        if (ext) extensionsSet.emplace(ext);
    }

    uint32_t availableExtCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, nullptr);
    std::vector<VkExtensionProperties> availableExts(availableExtCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, availableExts.data());

    bool hasPortabilityEnumeration = false;
    bool hasGetPhysicalDeviceProps2 = false;
    for (const auto& ext : availableExts) {
        if (std::string(ext.extensionName) == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) {
            hasPortabilityEnumeration = true;
        }
        if (std::string(ext.extensionName) == kGetPhysicalDeviceProps2Ext) {
            hasGetPhysicalDeviceProps2 = true;
        }
    }
    if (hasPortabilityEnumeration) extensionsSet.emplace(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    if (hasGetPhysicalDeviceProps2) extensionsSet.emplace(kGetPhysicalDeviceProps2Ext);

    std::vector<const char*> finalExtensions;
    finalExtensions.reserve(extensionsSet.size());
    for (const auto& ext : extensionsSet) finalExtensions.push_back(ext.c_str());

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(finalExtensions.size());
    createInfo.ppEnabledExtensionNames = finalExtensions.data();
    if (hasPortabilityEnumeration) {
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    if (hasValidationLayer()) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &validationLayer;
    }

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] vkCreateInstance returned %d\n", static_cast<int>(result));
        return false;
    }
    return true;
}

bool Renderer::createDescriptors()
{
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding lightsBinding{};
    lightsBinding.binding = 2;
    lightsBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightsBinding.descriptorCount = 1;
    lightsBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding shadowBinding{};
    shadowBinding.binding = 3;
    shadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    shadowBinding.descriptorCount = 1;
    shadowBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutBinding terrainAlbedoBinding{};
    terrainAlbedoBinding.binding = 4;
    terrainAlbedoBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    terrainAlbedoBinding.descriptorCount = 1;
    terrainAlbedoBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 3 only exists when the depth target came up; the "_lit" shader
    // variants used in that case never declare it. Binding 4 is always present
    // because both terrain fragment variants sample the layer array.
    std::vector<VkDescriptorSetLayoutBinding> bindings = {uboBinding, samplerBinding, lightsBinding};
    if (shadowMap_.valid()) bindings.push_back(shadowBinding);
    bindings.push_back(terrainAlbedoBinding);

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(deviceContext_.device(), &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] Failed to create descriptor set layout.\n");
        return false;
    }

    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, swapchain_.imageCount() * 2},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, swapchain_.imageCount() * 4}
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = swapchain_.imageCount();
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(deviceContext_.device(), &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] Failed to create descriptor pool.\n");
        return false;
    }

    descriptorSets_.resize(swapchain_.imageCount(), VK_NULL_HANDLE);
    std::vector<VkDescriptorSetLayout> layouts(swapchain_.imageCount(), descriptorSetLayout_);
    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = descriptorPool_;
    dsAlloc.descriptorSetCount = swapchain_.imageCount();
    dsAlloc.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(deviceContext_.device(), &dsAlloc, descriptorSets_.data()) != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] Failed to allocate descriptor sets.\n");
        return false;
    }

    return true;
}

bool Renderer::createPerFrameUniformBuffers()
{
    uniformBuffers_.resize(swapchain_.imageCount(), VK_NULL_HANDLE);
    uniformMemories_.resize(swapchain_.imageCount(), VK_NULL_HANDLE);
    lightBuffers_.resize(swapchain_.imageCount(), VK_NULL_HANDLE);
    lightMemories_.resize(swapchain_.imageCount(), VK_NULL_HANDLE);

    // Create default white texture for sampler binding
    if (!TextureLoader::createDefaultWhite(
            deviceContext_.physicalDevice(),
            deviceContext_.device(),
            deviceContext_.graphicsQueue(),
            frameGraph_.commandPool(),
            defaultTexture_)) {
        std::fprintf(stderr, "[D78] Failed to create default white texture.\n");
        return false;
    }

    // Non-fatal: without it the terrain shader falls back to the white texture.
    if (!createTerrainTextureSet(
            deviceContext_.physicalDevice(),
            deviceContext_.device(),
            deviceContext_.graphicsQueue(),
            frameGraph_.commandPool(),
            defaultTerrainTextureRoot(),
            terrainTextures_)) {
        std::fprintf(stderr, "[D78] Terrain texture arrays unavailable.\n");
    }

    for (uint32_t i = 0; i < swapchain_.imageCount(); ++i) {
        if (!createHostVisibleBuffer(
                deviceContext_.physicalDevice(),
                deviceContext_.device(),
                sizeof(CameraUBO),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                uniformBuffers_[i],
                uniformMemories_[i])) {
            std::fprintf(stderr, "[D78] Failed to create uniform buffer %u.\n", i);
            return false;
        }

        if (!createHostVisibleBuffer(
                deviceContext_.physicalDevice(),
                deviceContext_.device(),
                sizeof(SceneLightsUbo),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                lightBuffers_[i],
                lightMemories_[i])) {
            std::fprintf(stderr, "[D78] Failed to create scene light buffer %u.\n", i);
            return false;
        }

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers_[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(CameraUBO);

        VkDescriptorBufferInfo lightInfo{};
        lightInfo.buffer = lightBuffers_[i];
        lightInfo.offset = 0;
        lightInfo.range = sizeof(SceneLightsUbo);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = defaultTexture_.imageView;
        imageInfo.sampler = defaultTexture_.sampler;

        // The depth pass runs before the main pass of every frame, so by the
        // time a fragment samples this the image is already read-only.
        VkDescriptorImageInfo shadowInfo{};
        shadowInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        shadowInfo.imageView = shadowMap_.imageView();
        shadowInfo.sampler = shadowMap_.sampler();

        VkDescriptorImageInfo terrainAlbedoInfo{};
        terrainAlbedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        terrainAlbedoInfo.imageView = terrainTextures_.albedo.valid()
                                    ? terrainTextures_.albedo.view : defaultTexture_.imageView;
        terrainAlbedoInfo.sampler = terrainTextures_.albedo.valid()
                                  ? terrainTextures_.albedo.sampler : defaultTexture_.sampler;

        std::array<VkWriteDescriptorSet, 5> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets_[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &bufferInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets_[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &imageInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = descriptorSets_[i];
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &lightInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = descriptorSets_[i];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo = &shadowInfo;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = descriptorSets_[i];
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo = &terrainAlbedoInfo;

        // Without a shadow map binding 3 is absent, so its write is skipped by
        // moving the terrain write into its slot.
        if (!shadowMap_.valid()) writes[3] = writes[4];

        vkUpdateDescriptorSets(deviceContext_.device(),
                               shadowMap_.valid() ? 5u : 4u,
                               writes.data(), 0, nullptr);
    }

    return true;
}

bool Renderer::createPipeline()
{
    // The runtime layout carries the scene-light binding, so it uses the "_lit"
    // fragment variants; the editor viewport keeps the plain ones. With a live
    // depth target the layout also carries binding 3, which only the "_shadow"
    // variants declare.
    const std::string litSuffix = shadowMap_.valid() ? "_shadow.frag.spv" : "_lit.frag.spv";

    const std::string vert = std::string(VULKAN_SHADER_DIR) + "/basic.vert.spv";
    const std::string frag = std::string(VULKAN_SHADER_DIR) + "/basic" + litSuffix;

    std::string pipelineError;
    if (!PipelineBuilder::createBasicPipeline(
            deviceContext_.device(),
            swapchain_.extent(),
            hdr_.renderPass(),
            descriptorSetLayout_,
            vert,
            frag,
            pipelineLayout_,
            pipeline_,
            pipelineError)) {
        std::fprintf(stderr, "[D78] Pipeline creation failed: %s\n", pipelineError.c_str());
        return false;
    }

    // Create textured pipeline using the same layout but different shaders
    const std::string texVert = std::string(VULKAN_SHADER_DIR) + "/textured.vert.spv";
    const std::string texFrag = std::string(VULKAN_SHADER_DIR) + "/textured" + litSuffix;

    std::string texPipelineError;
    if (!PipelineBuilder::createBasicPipeline(
            deviceContext_.device(),
            swapchain_.extent(),
            hdr_.renderPass(),
            descriptorSetLayout_,
            texVert,
            texFrag,
            texturedPipelineLayout_,
            texturedPipeline_,
            texPipelineError)) {
        std::fprintf(stderr, "[D78] Textured pipeline creation failed: %s (non-fatal)\n",
                     texPipelineError.c_str());
        // Non-fatal — fall back to basic pipeline
    }

    // Create terrain pipeline for heightmap mesh rendering
    const std::string terrainVert = std::string(VULKAN_SHADER_DIR) + "/terrain.vert.spv";
    const std::string terrainFrag = std::string(VULKAN_SHADER_DIR)
                                  + (shadowMap_.valid() ? "/terrain_shadow.frag.spv"
                                                        : "/terrain.frag.spv");

    std::string terrainPipelineError;
    if (!PipelineBuilder::createTerrainPipeline(
            deviceContext_.device(),
            swapchain_.extent(),
            hdr_.renderPass(),
            descriptorSetLayout_,
            terrainVert,
            terrainFrag,
            terrainPipelineLayout_,
            terrainPipeline_,
            terrainPipelineError)) {
        std::fprintf(stderr, "[D78] Terrain pipeline creation failed: %s (non-fatal)\n",
                     terrainPipelineError.c_str());
        // Non-fatal — terrain will fall back to per-tile cube instances
    }

    // Create water pipeline for transparent water planes
    const std::string waterVert = std::string(VULKAN_SHADER_DIR) + "/water.vert.spv";
    const std::string waterFrag = std::string(VULKAN_SHADER_DIR) + "/water.frag.spv";

    std::string waterPipelineError;
    if (!PipelineBuilder::createWaterPipeline(
            deviceContext_.device(),
            swapchain_.extent(),
            hdr_.renderPass(),
            descriptorSetLayout_,
            waterVert,
            waterFrag,
            waterPipelineLayout_,
            waterPipeline_,
            waterPipelineError)) {
        std::fprintf(stderr, "[D78] Water pipeline creation failed: %s (non-fatal)\n",
                     waterPipelineError.c_str());
    }

    // Camera-facing sprites (RenderMode::BillboardSprite)
    const std::string billboardVert = std::string(VULKAN_SHADER_DIR) + "/billboard.vert.spv";
    const std::string billboardFrag = std::string(VULKAN_SHADER_DIR) + "/billboard.frag.spv";

    std::string billboardPipelineError;
    if (!PipelineBuilder::createBillboardPipeline(
            deviceContext_.device(),
            swapchain_.extent(),
            hdr_.renderPass(),
            descriptorSetLayout_,
            billboardVert,
            billboardFrag,
            billboardPipelineLayout_,
            billboardPipeline_,
            billboardPipelineError)) {
        std::fprintf(stderr, "[D78] Billboard pipeline creation failed: %s (non-fatal)\n",
                     billboardPipelineError.c_str());
    }

    return true;
}

bool Renderer::init(WindowContext& window)
{
    if (!createInstance(window.requiredVulkanExtensions())) return false;
    if (!window.createSurface(instance_, surface_)) return false;
    if (!deviceContext_.init(instance_, surface_)) return false;
    swapchain_.setPreferSrgb(true);
    if (!swapchain_.init(deviceContext_, surface_, window.handle())) return false;

    // Before the pipelines: every scene pipeline is built against the HDR pass,
    // not the swapchain one.
    if (!hdr_.init(deviceContext_.device())) return false;
    if (!hdr_.createResources(deviceContext_.physicalDevice(), deviceContext_.device(),
                              swapchain_.extent().width, swapchain_.extent().height)) {
        return false;
    }
    // Resolve target is the swapchain, which is _SRGB here, so the tonemap
    // writes linear and the presentation engine encodes.
    if (!hdr_.createPipeline(deviceContext_.device(), swapchain_.renderPass(), VULKAN_SHADER_DIR)) {
        return false;
    }

    // Before the descriptors: whether the depth target came up decides if the
    // scene layout declares binding 3 and which fragment variants are loaded.
    shadowMap_.initTarget(deviceContext_.physicalDevice(), deviceContext_.device());

    if (!createDescriptors()) return false;
    if (!createPipeline()) return false;

    if (!meshBuffers_.initCube(deviceContext_.physicalDevice(), deviceContext_.device())) {
        std::fprintf(stderr, "[D78] Failed to create cube buffers.\n");
        return false;
    }

    if (!frameGraph_.init(
            deviceContext_.device(),
            deviceContext_.queueFamilies().graphicsFamily.value(),
            deviceContext_.graphicsQueue(),
            deviceContext_.presentQueue(),
            swapchain_.swapchain(),
            swapchain_.extent(),
            swapchain_.renderPass(),
            swapchain_.imageViews(),
            swapchain_.depthImageView())) {
        return false;
    }

    if (!createPerFrameUniformBuffers()) return false;

    // Skinning is optional: without it animated meshes just draw in bind pose.
    if (createBoneResources()) {
        std::string skinErr;
        const std::string shaderDir = VULKAN_SHADER_DIR;
        const std::string skinFrag = shadowMap_.valid() ? "/skinned_shadow.frag.spv"
                                                        : "/skinned_lit.frag.spv";
        if (!PipelineBuilder::createSkinnedPipeline(
                deviceContext_.device(), swapchain_.extent(), hdr_.renderPass(),
                descriptorSetLayout_, boneSetLayout_,
                shaderDir + "/skinned.vert.spv", shaderDir + skinFrag,
                skinnedPipelineLayout_, skinnedPipeline_, skinErr)) {
            std::fprintf(stderr, "[Skin] Skinned pipeline unavailable: %s\n", skinErr.c_str());
            skinnedPipeline_ = VK_NULL_HANDLE;
            skinnedPipelineLayout_ = VK_NULL_HANDLE;
        }
    }

    // Depth-only casters. Needs boneSetLayout_, so it runs after the skinning
    // resources; without them skinned casters fall back to their bind pose.
    if (shadowMap_.valid()) {
        shadowMap_.createPipelines(deviceContext_.device(), VULKAN_SHADER_DIR,
                                   descriptorSetLayout_, boneSetLayout_);
    }

    if (!physicsWorld_.init()) {
        std::fprintf(stderr, "[D80] PhysicsWorld initialization failed.\n");
        return false;
    }
    physicsWorld_.setGravity({0.0f, -9.8f, 0.0f});
    physicsWorld_.setRestitution(0.20f);
    physicsWorld_.setCollisionCallback([this](const dash::physics::CollisionEvent& ev) {
        auto entityOf = [this](int bodyId) -> uint64_t {
            auto it = bodyToEntity_.find(bodyId);
            return it == bodyToEntity_.end() ? 0ull : it->second;
        };

        ::CollisionEvent out;
        switch (ev.type) {
            case dash::physics::CollisionEventType::Enter: out.phase = ::CollisionEvent::Phase::Enter; break;
            case dash::physics::CollisionEventType::Stay:  out.phase = ::CollisionEvent::Phase::Stay;  break;
            default:                                       out.phase = ::CollisionEvent::Phase::Exit;  break;
        }
        out.entityA = entityOf(ev.a);
        out.entityB = entityOf(ev.b);
        events_.emit(out);

        if (ev.type == dash::physics::CollisionEventType::Enter) {
            std::printf("[D82] Collision Enter: %d <-> %d (entities %llu <-> %llu)\n",
                        ev.a, ev.b,
                        static_cast<unsigned long long>(out.entityA),
                        static_cast<unsigned long long>(out.entityB));
        }
    });

    floorBodyId_ = physicsWorld_.createStaticPlane(-0.7f);

    dash::physics::Vec3 spawn{0.0f, 0.8f, 0.0f};
    bool loadedSceneSpawn = false;
    if (!scenePath_.empty()) {
        const LoadedScene loadedScene = SceneLoader::load(scenePath_);
        if (!loadedScene.valid) {
            std::fprintf(stderr, "[VSTEP] Could not load scene: %s\n", scenePath_.c_str());
        }

        dash::physics::Vec3 sceneSpawn{};
        if (SceneLoader::loadSpawnPoint(loadedScene, sceneSpawn)) {
            spawn = sceneSpawn;
            loadedSceneSpawn = true;
            std::printf("[D84] Loaded scene spawn from %s -> (%.3f, %.3f, %.3f)\n",
                        scenePath_.c_str(), spawn.x, spawn.y, spawn.z);
        } else {
            std::printf("[D84] Could not parse scene spawn from %s (using default).\n", scenePath_.c_str());
        }

        sceneInstances_ = SceneLoader::loadInstances(loadedScene);
        sceneLights_ = SceneLoader::buildLights(loadedScene.data);
        terrainInstances_ = SceneLoader::loadTerrainInstances(loadedScene, &terrainHeightMap_, &terrainMapWidth_, &terrainMapHeight_);
        std::fprintf(stderr, "[VSTEP] scene instances loaded: %zu\n", sceneInstances_.size());
        std::fprintf(stderr, "[VSTEP] terrain instances loaded: %zu\n", terrainInstances_.size());

        // Build heightmap polygon mesh for Vulkan terrain rendering
        if (terrainPipeline_ != VK_NULL_HANDLE) {
            const unsigned int sceneSeed = loadedScene.data.worldSeed;
            TerrainMesh terrainMesh;
            terrainMesh.generate(sceneSeed);
            std::vector<TerrainVkVertex> terrainVerts;
            std::vector<uint32_t> terrainIndices;
            terrainMesh.buildVulkanMesh(terrainVerts, terrainIndices);
            // Append cliff walls to same buffer
            terrainMesh.buildCliffWalls(terrainVerts, terrainIndices);

            if (!terrainVerts.empty() && !terrainIndices.empty()) {
                if (terrainMeshBuffers_.initFromData(
                        deviceContext_.physicalDevice(),
                        deviceContext_.device(),
                        terrainVerts.data(),
                        static_cast<uint32_t>(terrainVerts.size() * sizeof(TerrainVkVertex)),
                        terrainIndices.data(),
                        static_cast<uint32_t>(terrainIndices.size() * sizeof(uint32_t)),
                        static_cast<uint32_t>(terrainIndices.size()))) {
                    std::fprintf(stderr, "[VSTEP] Terrain mesh uploaded: %zu verts, %zu indices\n",
                                 terrainVerts.size(), terrainIndices.size());
                    // Clear old tile instances — terrain mesh replaces them
                    terrainInstances_.clear();
                    // Keep TerrainMesh for height sampling
                    terrainMesh_ = std::move(terrainMesh);
                    terrainMeshReady_ = true;

                    // Build water mesh if water pipeline is available
                    if (waterPipeline_ != VK_NULL_HANDLE) {
                        // Add default water body
                        WaterBody defaultWater;
                        defaultWater.id = 1;
                        defaultWater.waterLevel = 0.3f * INTRA_CLIFF_HEIGHT;
                        defaultWater.opacity = 0.6f;
                        defaultWater.tint = {0.08f, 0.14f, 0.31f};
                        terrainMesh_.addWaterBody(defaultWater);

                        std::vector<TerrainVkVertex> waterVerts;
                        std::vector<uint32_t> waterIndices;
                        terrainMesh_.buildWaterMesh(waterVerts, waterIndices);
                        if (!waterVerts.empty()) {
                            waterMeshBuffers_.initFromData(
                                deviceContext_.physicalDevice(),
                                deviceContext_.device(),
                                waterVerts.data(),
                                static_cast<uint32_t>(waterVerts.size() * sizeof(TerrainVkVertex)),
                                waterIndices.data(),
                                static_cast<uint32_t>(waterIndices.size() * sizeof(uint32_t)),
                                static_cast<uint32_t>(waterIndices.size()));
                            std::fprintf(stderr, "[VSTEP] Water mesh uploaded: %zu verts, %zu indices\n",
                                         waterVerts.size(), waterIndices.size());
                        }
                    }
                } else {
                    std::fprintf(stderr, "[VSTEP] Failed to upload terrain mesh buffers\n");
                }
            }
        }

            // Load player position for WASD movement
            player_.loadFromScene(loadedScene, &terrainMesh_, terrainMeshReady_,
                                  terrainHeightMap_, terrainMapWidth_, terrainMapHeight_);

        snapInstancesToTerrain();
        spawnSceneryPhysicsBodies(loadedScene);
    }

    if (sceneInstances_.empty()) {
        sceneInstances_.push_back({spawn, {0.26f, 0.52f, 0.26f}, {0.30f, 0.58f, 0.95f}});
    }

    resolveSceneMeshes();
    resolveSceneMaterials();
    buildSceneAnimators();

    // No scene ships a LightComponent yet, so without this the entity pass would
    // fall back to its hardcoded headlight and nothing would ever cast. The sun
    // mirrors the editor lighting the terrain shader already uses; LightingParams
    // stores the surface-to-light vector, SceneLight the emission direction.
    if (sceneLights_.empty()) {
        const LightingParams& lt = editorBridge_.lighting();
        SceneLight sun;
        sun.type = 0;
        sun.dirX = -lt.dirX;
        sun.dirY = -lt.dirY;
        sun.dirZ = -lt.dirZ;
        sun.colorR = lt.colorR;
        sun.colorG = lt.colorG;
        sun.colorB = lt.colorB;
        sun.intensity = lt.intensity;
        sun.castsShadows = true;
        sceneLights_.push_back(sun);
    }

    setupShadowLight();
    // Generate a small checkerboard floor when no terrain was loaded
    if (terrainInstances_.empty()) {
        for (int z = -3; z <= 3; ++z) {
            for (int x = -3; x <= 3; ++x) {
                const bool checker = ((x + z) & 1) == 0;
                terrainInstances_.push_back({
                    {static_cast<float>(x), -0.7f, static_cast<float>(z)},
                    {0.48f, 0.03f, 0.48f},
                    checker ? dash::physics::Vec3{0.24f, 0.34f, 0.24f}
                            : dash::physics::Vec3{0.18f, 0.28f, 0.18f}
                });
            }
        }
    }

    cubeBodyId_ = physicsWorld_.createDynamicBox(spawn, {0.30f, 0.30f, 0.30f}, 1.0f);
    transformProxy_.syncFromPhysics(physicsWorld_, cubeBodyId_, cubeTransform_);

    // In standalone mode there is no editor camera sync file, so align camera
    // to the loaded scene spawn to avoid starting with an empty/black view.
    if (loadedSceneSpawn) {
        camera_.focusOnSpawn(spawn);
    } else if (scenePath_.empty()) {
        // No scene provided: position camera to see the default physics cube
        camera_.focusOnSpawn(spawn);
    }

    dash::physics::DebugPhysicsDraw::logBodyAabb(physicsWorld_, floorBodyId_, "floor");
    dash::physics::DebugPhysicsDraw::logBodyAabb(physicsWorld_, cubeBodyId_, "cube");

    initialized_ = true;
    std::puts("[D78] Renderer + FrameGraphLite initialized.");
    std::puts("[D80-D83] PhysicsWorld active (fixed-step + cube/plane baseline).");
    return true;
}

bool Renderer::updateCameraUbo(uint32_t imageIndex)
{
    const float aspect = static_cast<float>(swapchain_.extent().width)
                       / static_cast<float>(swapchain_.extent().height);

    CameraUBO ubo{};
    ubo.viewProj = camera_.computeViewProjection(aspect);

    void* mapped = nullptr;
    if (vkMapMemory(deviceContext_.device(), uniformMemories_[imageIndex], 0, sizeof(CameraUBO), 0, &mapped) != VK_SUCCESS) {
        return false;
    }
    std::memcpy(mapped, &ubo, sizeof(CameraUBO));
    vkUnmapMemory(deviceContext_.device(), uniformMemories_[imageIndex]);
    return true;
}

bool Renderer::updateSceneLightsUbo(uint32_t imageIndex)
{
    if (imageIndex >= lightMemories_.size() || lightMemories_[imageIndex] == VK_NULL_HANDLE) {
        return false;
    }

    SceneLightsUbo ubo{};
    packSceneLights(&sceneLights_, {camera_.x(), camera_.y(), camera_.z()}, ubo);
    for (int i = 0; i < kShadowCascades; ++i) {
        std::memcpy(ubo.shadowMatrices[i], shadowMatrices_[i].m, sizeof(shadowMatrices_[i].m));
    }
    std::memcpy(ubo.shadowSplits, shadowSplits_, sizeof(ubo.shadowSplits));
    std::memcpy(ubo.shadowTexels, shadowTexels_, sizeof(ubo.shadowTexels));
    std::memcpy(ubo.shadowDepthBias, shadowDepthBias_, sizeof(ubo.shadowDepthBias));
    std::memcpy(ubo.shadowParams, shadowParams_, sizeof(ubo.shadowParams));

    void* mapped = nullptr;
    if (vkMapMemory(deviceContext_.device(), lightMemories_[imageIndex], 0, sizeof(SceneLightsUbo), 0, &mapped) != VK_SUCCESS) {
        return false;
    }
    std::memcpy(mapped, &ubo, sizeof(SceneLightsUbo));
    vkUnmapMemory(deviceContext_.device(), lightMemories_[imageIndex]);
    return true;
}

void Renderer::setupShadowLight()
{
    shadowLightIndex_ = -1;
    for (Mat4& m : shadowMatrices_) m = Mat4{};
    for (float& p : shadowParams_) p = 0.0f;

    if (!shadowMap_.valid()) return;

    for (size_t i = 0; i < sceneLights_.size(); ++i) {
        if (sceneLights_[i].type == 0 && sceneLights_[i].castsShadows) {
            shadowLightIndex_ = static_cast<int>(i);
            break;
        }
    }
    if (shadowLightIndex_ < 0) {
        std::puts("[Shadow] No directional light declares castsShadows; shadows disabled.");
        return;
    }

    shadowParams_[0] = static_cast<float>(shadowLightIndex_ + 1);
    shadowParams_[1] = 1.0f / static_cast<float>(ShadowMap::kResolution);
    // Cross-fade over the last tenth of each cascade: wide enough to hide the
    // change of resolution, narrow enough that the double lookup is rare.
    shadowParams_[2] = 0.10f;

    updateShadowMatrix();

    std::printf("[Shadow] Light %d casts over %d cascades to %.0f units"
                " (%.0f / %.0f / %.0f mm per texel).\n",
                shadowLightIndex_, kShadowCascades, kShadowMaxDistance,
                shadowTexels_[0] * 1000.0f,
                shadowTexels_[1] * 1000.0f,
                shadowTexels_[2] * 1000.0f);
}

void Renderer::updateShadowMatrix()
{
    if (shadowLightIndex_ < 0) return;

    const float aspect = static_cast<float>(swapchain_.extent().width)
                       / static_cast<float>(swapchain_.extent().height);

    const float yaw = camera_.yawDegrees() * 0.0174532925f;
    const float pitch = camera_.pitchDegrees() * 0.0174532925f;
    const Vec3 forward{std::cos(yaw) * std::cos(pitch),
                       std::sin(pitch),
                       std::sin(yaw) * std::cos(pitch)};
    const Vec3 camPos{camera_.x(), camera_.y(), camera_.z()};
    const Vec3 right = camera_.rightVector();
    const Vec3 up = camera_.upVector();

    float splits[kShadowCascades];
    computeCascadeSplits(0.5f, kShadowMaxDistance, 0.7f, splits);

    const SceneLight& light = sceneLights_[static_cast<size_t>(shadowLightIndex_)];
    const Vec3 dir{light.dirX, light.dirY, light.dirZ};

    float sliceNear = 0.1f;
    for (int i = 0; i < kShadowCascades; ++i) {
        const ShadowVolume slice = frustumSliceVolume(
            camPos, forward, right, up, 60.0f * 0.0174532925f, aspect,
            sliceNear, splits[i]);

        const ShadowVolume snapped =
            snapVolumeToTexelGrid(dir, slice, ShadowMap::kResolution);
        shadowMatrices_[i] = directionalLightMatrix(dir, snapped);

        const float texel = shadowTexelWorldSize(snapped, ShadowMap::kResolution);
        shadowSplits_[i] = splits[i];
        shadowTexels_[i] = texel;
        // The ortho depth range spans 2r world units, so a 1.5-texel world
        // offset is that same fraction of a light clip unit.
        shadowDepthBias_[i] = (texel * 1.5f) / (2.0f * snapped.radius);

        sliceNear = splits[i];
    }
}

void Renderer::recordShadowPass(VkCommandBuffer cmd, uint32_t imageIndex,
                                const std::vector<InstanceResources>& res)
{
    if (!shadowMap_.valid()) return;

    const bool draws = shadowLightIndex_ >= 0 && shadowMap_.hasPipelines();
    const bool terrainCasts = draws
                           && shadowMap_.terrainPipeline() != VK_NULL_HANDLE
                           && terrainMeshBuffers_.indexCount() > 0;

    // Billboards are vertical cutouts, so the depth quad only spins around Y to
    // face the light: keeping its up axis world-vertical is what anchors the
    // shadow at the foot of the sprite and stretches it with the sun elevation.
    // A quad fully perpendicular to the light would instead lift off the ground
    // and project an unstretched, detached silhouette.
    Vec3 lightRight{1.0f, 0.0f, 0.0f};
    if (shadowLightIndex_ >= 0) {
        const SceneLight& light = sceneLights_[static_cast<size_t>(shadowLightIndex_)];
        const Vec3 axis = cross(Vec3{light.dirX, light.dirY, light.dirZ}, Vec3{0.0f, 1.0f, 0.0f});
        const Vec3 normalized = normalize(axis);
        if (dot(normalized, normalized) > 0.5f) lightRight = normalized;
    }

    for (int cascade = 0; cascade < kShadowCascades; ++cascade) {
        // Entered even with no caster: the clear is what leaves the layer in
        // the layout the descriptor written at init already promises.
        shadowMap_.beginPass(cmd, cascade);

        if (terrainCasts) {
            // First, and the biggest occluder by far: filling depth here lets
            // early-Z reject most of the instance fragments that follow.
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMap_.terrainPipeline());
            VkBuffer terrainVB[] = { terrainMeshBuffers_.vertexBuffer() };
            VkDeviceSize terrainOffsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, terrainVB, terrainOffsets);
            vkCmdBindIndexBuffer(cmd, terrainMeshBuffers_.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            // Terrain vertices are already in world space.
            float terrainPC[kShadowPushConstantFloats];
            const Mat4 model = identity();
            std::memcpy(terrainPC, model.m, sizeof(model.m));
            std::memcpy(terrainPC + 16, shadowMatrices_[cascade].m, sizeof(shadowMatrices_[cascade].m));
            vkCmdPushConstants(cmd, shadowMap_.terrainPipelineLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(terrainPC), terrainPC);

            vkCmdDrawIndexed(cmd, terrainMeshBuffers_.indexCount(), 1, 0, 0, 0);
        }

        if (draws) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowMap_.pipeline());

            VkBuffer vertexBuffers[] = { meshBuffers_.vertexBuffer() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(cmd, meshBuffers_.indexBuffer(), 0, meshBuffers_.indexType());

            SceneDrawParams shadowParams;
            shadowParams.depthOnly       = true;
            shadowParams.opaquePipeline  = shadowMap_.pipeline();
            shadowParams.opaqueLayout    = shadowMap_.pipelineLayout();
            shadowParams.skinnedPipeline = shadowMap_.skinnedPipeline();
            shadowParams.skinnedLayout   = shadowMap_.skinnedPipelineLayout();
            // Alpha-cut depth variant: same call site, different pipeline.
            shadowParams.billboardPipeline = shadowMap_.billboardPipeline();
            shadowParams.billboardLayout   = shadowMap_.billboardPipelineLayout();
            shadowParams.boneSet         = boneSet_;
            shadowParams.bonePalette     = &bonePalette_;
            shadowParams.defaultSet      = descriptorSets_[imageIndex];
            shadowParams.fallbackMesh    = &meshBuffers_;
            // Culling then happens against this cascade's frustum, so a caster
            // is only recorded into the maps that can actually see it.
            shadowParams.viewProj        = shadowMatrices_[cascade];
            shadowParams.cameraRight     = lightRight;
            shadowParams.cameraUp        = Vec3{0.0f, 1.0f, 0.0f};

            drawSceneInstances(cmd, sceneInstances_, res, editorBridge_.lighting(), shadowParams);
        }

        shadowMap_.endPass(cmd);
    }
}

// Locates a mesh id on disk using the model dir, the scene dir and the raw path
// in that order. Returns an empty string when nothing matches.
std::string Renderer::resolveModelPath(const std::string& meshId) const
{
    if (meshId.empty() || meshId == "cube") return {};

    namespace fs = std::filesystem;
    std::error_code ec;

    std::vector<fs::path> candidates;
    const fs::path raw(meshId);
    if (raw.is_absolute()) {
        candidates.push_back(raw);
    } else {
        candidates.push_back(fs::path(VULKAN_MODEL_DIR) / raw);
        if (!scenePath_.empty()) {
            candidates.push_back(fs::path(scenePath_).parent_path() / raw);
        }
        candidates.push_back(raw);
    }

    for (const auto& c : candidates) {
        if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) return c.string();
        ec.clear();
    }
    return {};
}

// Resolves a mesh id to GPU buffers, loading and caching it on first use.
// Returns nullptr to signal "use the builtin cube".
const MeshBuffers* Renderer::resolveMesh(const std::string& meshId)
{
    if (meshId.empty() || meshId == "cube") return nullptr;

    if (CachedModel* cached = assetCache_.get(meshId)) {
        return cached->meshBuffers.indexCount() > 0 ? &cached->meshBuffers : nullptr;
    }

    const std::string resolved = resolveModelPath(meshId);

    CachedModel model;
    if (resolved.empty()) {
        std::fprintf(stderr, "[AssetCache3D] Mesh not found: '%s' (using builtin cube)\n",
                     meshId.c_str());
    } else {
        // .dashmesh carries the skinning stream; anything else goes through Assimp.
        const bool isDashMesh = std::filesystem::path(resolved).extension() == ".dashmesh";
        const bool loaded = isDashMesh
            ? model.meshBuffers.initFromDashMesh(deviceContext_.physicalDevice(),
                                                 deviceContext_.device(), resolved)
            : model.meshBuffers.initFromGLTF(deviceContext_.physicalDevice(),
                                             deviceContext_.device(), resolved);
        if (!loaded) {
            std::fprintf(stderr, "[AssetCache3D] Failed to load mesh: %s (using builtin cube)\n",
                         resolved.c_str());
            model.meshBuffers.shutdown(deviceContext_.device());
        }
    }

    // Cache failures too, so a broken reference is not retried every load.
    CachedModel& stored = assetCache_.store(meshId, std::move(model));
    return stored.meshBuffers.indexCount() > 0 ? &stored.meshBuffers : nullptr;
}

void Renderer::resolveSceneMeshes()
{
    // Layer drives draw order (transparency); mesh/material grouping only
    // reduces rebinds within a layer.
    std::stable_sort(sceneInstances_.begin(), sceneInstances_.end(),
                     [](const RenderInstance& a, const RenderInstance& b) {
                         if (a.layer != b.layer) return a.layer < b.layer;
                         if (a.meshId != b.meshId) return a.meshId < b.meshId;
                         return a.materialId < b.materialId;
                     });

    sceneInstanceMeshes_.clear();
    sceneInstanceMeshes_.reserve(sceneInstances_.size());
    for (const auto& inst : sceneInstances_) {
        sceneInstanceMeshes_.push_back(resolveMesh(inst.meshId));
    }
}

void Renderer::buildSceneAnimators()
{
    animators_ = buildAnimators(sceneInstances_, animationSets_,
                                [this](const std::string& meshId) {
                                    return resolveModelPath(meshId);
                                });

    if (!animators_.empty()) {
        std::printf("[Anim] %zu animated instance(s) over %zu model(s).\n",
                    animators_.size(), animationSets_.size());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Bone palette — one dynamic-offset UBO shared by every skinned draw. Slots are
// padded to the device alignment so a whole frame fits in a single buffer.
// ─────────────────────────────────────────────────────────────────────────────
bool Renderer::createBoneResources()
{
    VkDevice dev = deviceContext_.device();

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(deviceContext_.physicalDevice(), &props);
    const VkDeviceSize align = std::max<VkDeviceSize>(
        props.limits.minUniformBufferOffsetAlignment, 1);

    constexpr uint32_t kSlots = 64;
    const uint32_t stride = static_cast<uint32_t>(
        ((dash::anim::kBonePaletteBytes + align - 1) / align) * align);

    if (!createHostVisibleBuffer(deviceContext_.physicalDevice(), dev,
                                 static_cast<VkDeviceSize>(stride) * kSlots,
                                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                 boneBuffer_, boneMemory_)) {
        std::fprintf(stderr, "[Skin] Failed to create bone palette buffer.\n");
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(dev, boneMemory_, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
        std::fprintf(stderr, "[Skin] Failed to map bone palette buffer.\n");
        return false;
    }
    bonePalette_.mapped = static_cast<unsigned char*>(mapped);
    bonePalette_.slotStride = stride;
    bonePalette_.slotCount = kSlots;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &boneSetLayout_) != VK_SUCCESS)
        return false;

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    poolSize.descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(dev, &poolInfo, nullptr, &boneDescriptorPool_) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = boneDescriptorPool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &boneSetLayout_;
    if (vkAllocateDescriptorSets(dev, &alloc, &boneSet_) != VK_SUCCESS)
        return false;

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = boneBuffer_;
    bufInfo.offset = 0;
    bufInfo.range = dash::anim::kBonePaletteBytes;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = boneSet_;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufInfo;
    vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

    std::printf("[Skin] Bone palette ready: %u slots of %u bytes (%u bones max).\n",
                kSlots, stride, dash::anim::kBonePaletteMatrixCount);
    return true;
}

void Renderer::destroyBoneResources()
{
    VkDevice dev = deviceContext_.device();
    if (dev == VK_NULL_HANDLE) return;

    if (boneDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, boneDescriptorPool_, nullptr);
        boneDescriptorPool_ = VK_NULL_HANDLE;
        boneSet_ = VK_NULL_HANDLE;
    }
    if (boneSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, boneSetLayout_, nullptr);
        boneSetLayout_ = VK_NULL_HANDLE;
    }
    if (bonePalette_.mapped != nullptr) {
        vkUnmapMemory(dev, boneMemory_);
        bonePalette_ = dash::anim::BonePalette{};
    }
    if (boneBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(dev, boneBuffer_, nullptr);
        boneBuffer_ = VK_NULL_HANDLE;
    }
    if (boneMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(dev, boneMemory_, nullptr);
        boneMemory_ = VK_NULL_HANDLE;
    }
    PipelineBuilder::destroy(dev, skinnedPipelineLayout_, skinnedPipeline_);
    skinnedPipelineLayout_ = VK_NULL_HANDLE;
    skinnedPipeline_ = VK_NULL_HANDLE;
}

void Renderer::updateSkinnedInstances(float dt, std::vector<InstanceResources>& res)
{
    if (!bonePalette_.usable()) return;
    bonePalette_.reset();

    for (auto& [index, player] : animators_) {
        if (index >= res.size() || index >= sceneInstances_.size()) continue;
        // Re-reading the component keeps Inspector edits (clip, pause, speed) live.
        player.syncWithComponent(sceneInstances_[index].animation);
        player.update(dt);

        const std::vector<dash::anim::Mat4>& mats = player.boneMatrices();
        if (mats.empty()) continue;

        const int64_t offset = bonePalette_.writeSlot(mats.front().m,
                                                      static_cast<uint32_t>(mats.size()));
        if (offset < 0) break;  // frame ran out of slots

        res[index].boneMatrices = mats.front().m;
        res[index].boneCount = static_cast<uint32_t>(mats.size());
        res[index].boneOffset = static_cast<uint32_t>(offset);
    }
}

void Renderer::destroySceneMaterials()
{    VkDevice dev = deviceContext_.device();
    if (dev == VK_NULL_HANDLE) return;

    for (auto& m : materials_) {
        if (m.ownsTexture) TextureLoader::destroy(dev, m.texture);
    }
    materials_.clear();
    sceneInstanceMaterials_.clear();

    if (materialDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, materialDescriptorPool_, nullptr);
        materialDescriptorPool_ = VK_NULL_HANDLE;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// materialDb — asset database behind GUID material references. Loaded lazily
// and at most once; a missing database is not an error, it just means only
// path-based material references can be resolved.
// ─────────────────────────────────────────────────────────────────────────────
const AssetDatabase* Renderer::materialDb()
{
    if (!assetDbLoadAttempted_) {
        assetDbLoadAttempted_ = true;
        const std::filesystem::path dbPath =
            std::filesystem::path(VULKAN_ASSETS_DIR) / "asset_db.json";
        assetDbLoaded_ = assetDb_.load(dbPath.string());
        if (!assetDbLoaded_) {
            std::fprintf(stderr, "[Material] Asset database unavailable at '%s'; "
                                 "GUID references will not resolve.\n", dbPath.string().c_str());
        }
    }
    return assetDbLoaded_ ? &assetDb_ : nullptr;
}

bool Renderer::resolveSceneMaterials()
{
    destroySceneMaterials();

    sceneInstanceMaterials_.assign(sceneInstances_.size(), -1);

    // Collect distinct material ids. "default" is the RenderComponent default
    // value and means "no material asset", same as empty.
    std::unordered_map<std::string, int> indexById;
    for (size_t i = 0; i < sceneInstances_.size(); ++i) {
        const std::string& id = sceneInstances_[i].materialId;
        if (id.empty() || id == "default") continue;

        auto it = indexById.find(id);
        if (it == indexById.end()) {
            const int idx = static_cast<int>(materials_.size());
            indexById.emplace(id, idx);
            materials_.push_back(MaterialGpu{});
            materials_.back().asset.name = id;
            sceneInstanceMaterials_[i] = idx;
        } else {
            sceneInstanceMaterials_[i] = it->second;
        }
    }

    if (materials_.empty()) return true;

    namespace fs = std::filesystem;
    VkDevice dev = deviceContext_.device();
    std::error_code ec;

    // Load each material definition and its albedo texture.
    for (auto& [id, idx] : indexById) {
        MaterialGpu& mat = materials_[static_cast<size_t>(idx)];

        std::vector<fs::path> candidates;
        const fs::path raw(id);

        // A GUID reference resolves through the asset database. Tried first so a
        // file that happens to share the name cannot shadow it; path references
        // keep working through the fallbacks below.
        if (looksLikeGuid(id)) {
            if (const AssetDatabase* db = materialDb()) {
                if (const AssetRecord* rec = db->findByGuid(id); rec && !rec->sourcePath.empty())
                    candidates.push_back(fs::path(VULKAN_ASSETS_DIR) / rec->sourcePath);
            }
        }

        if (raw.is_absolute()) {
            candidates.push_back(raw);
        } else {
            if (!scenePath_.empty())
                candidates.push_back(fs::path(scenePath_).parent_path() / raw);
            candidates.push_back(fs::path(VULKAN_MODEL_DIR) / raw);
            candidates.push_back(fs::path(VULKAN_ASSETS_DIR) / raw);
            candidates.push_back(raw);
        }

        bool loaded = false;
        for (const auto& c : candidates) {
            if (fs::exists(c, ec) && fs::is_regular_file(c, ec) && mat.asset.loadFromFile(c.string())) {
                loaded = true;
                break;
            }
            ec.clear();
        }
        if (!loaded) {
            std::fprintf(stderr, "[Material] Definition not found: '%s' (using defaults)\n", id.c_str());
        }

        // Resolve the albedo texture relative to the material file when possible.
        if (!mat.asset.albedoTexture.empty()) {
            std::vector<fs::path> texCandidates;
            const fs::path texRaw(mat.asset.albedoTexture);
            if (texRaw.is_absolute()) {
                texCandidates.push_back(texRaw);
            } else {
                texCandidates.push_back(fs::path(VULKAN_MODEL_DIR) / texRaw);
                if (!scenePath_.empty())
                    texCandidates.push_back(fs::path(scenePath_).parent_path() / texRaw);
                texCandidates.push_back(texRaw);
            }

            for (const auto& c : texCandidates) {
                if (!fs::exists(c, ec) || !fs::is_regular_file(c, ec)) { ec.clear(); continue; }
                if (TextureLoader::loadFromFile(deviceContext_.physicalDevice(), dev,
                                                deviceContext_.graphicsQueue(),
                                                frameGraph_.commandPool(),
                                                c.string(), mat.texture)) {
                    mat.ownsTexture = true;
                }
                break;
            }
            if (!mat.ownsTexture) {
                std::fprintf(stderr, "[Material] '%s': albedo texture '%s' unavailable (using white)\n",
                             id.c_str(), mat.asset.albedoTexture.c_str());
            }
        }
    }

    // One descriptor set per material per swapchain image.
    const uint32_t images = swapchain_.imageCount();
    const uint32_t setCount = static_cast<uint32_t>(materials_.size()) * images;

    // Has to mirror the binding list built in createDescriptors(): every set
    // carries the material sampler and the terrain array, plus the shadow map
    // when the depth target came up. Undercounting here fails the allocation
    // for every scene that references a material.
    const uint32_t samplersPerSet = shadowMap_.valid() ? 3u : 2u;

    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, setCount * 2},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount * samplersPerSet}
    }};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = setCount;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(dev, &poolInfo, nullptr, &materialDescriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "[Material] Failed to create descriptor pool.\n");
        destroySceneMaterials();
        return false;
    }

    for (auto& mat : materials_) {
        mat.sets.assign(images, VK_NULL_HANDLE);
        std::vector<VkDescriptorSetLayout> layouts(images, descriptorSetLayout_);
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = materialDescriptorPool_;
        alloc.descriptorSetCount = images;
        alloc.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(dev, &alloc, mat.sets.data()) != VK_SUCCESS) {
            std::fprintf(stderr, "[Material] Failed to allocate descriptor sets.\n");
            destroySceneMaterials();
            return false;
        }

        const TextureResource& tex = mat.ownsTexture ? mat.texture : defaultTexture_;
        for (uint32_t i = 0; i < images; ++i) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uniformBuffers_[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(CameraUBO);

            VkDescriptorBufferInfo lightInfo{};
            lightInfo.buffer = lightBuffers_[i];
            lightInfo.offset = 0;
            lightInfo.range = sizeof(SceneLightsUbo);

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = tex.imageView;
            imageInfo.sampler = tex.sampler;

            std::array<VkWriteDescriptorSet, 3> writes{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = mat.sets[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].descriptorCount = 1;
            writes[0].pBufferInfo = &bufferInfo;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = mat.sets[i];
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].descriptorCount = 1;
            writes[1].pImageInfo = &imageInfo;

            writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[2].dstSet = mat.sets[i];
            writes[2].dstBinding = 2;
            writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[2].descriptorCount = 1;
            writes[2].pBufferInfo = &lightInfo;

            vkUpdateDescriptorSets(dev, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }
    }

    std::fprintf(stderr, "[Material] Resolved %zu material(s).\n", materials_.size());
    return true;
}

void Renderer::spawnSceneryPhysicsBodies(const LoadedScene& scene)
{
    bodyToEntity_.clear();

    const auto spawns = SceneLoader::loadPhysicsBodies(scene);
    for (const auto& s : spawns) {
        // Static bodies get zero mass so the builtin backend keeps them fixed.
        const int bodyId = physicsWorld_.createDynamicBox(
            s.position, s.halfExtents, s.isStatic ? 0.0f : s.mass);
        if (bodyId < 0) continue;
        bodyToEntity_[bodyId] = s.entityId;
    }

    if (!spawns.empty()) {
        std::fprintf(stderr, "[Physics] Spawned %zu body(ies) from PhysicsComponent.\n",
                     spawns.size());
    }
}

void Renderer::snapInstancesToTerrain()
{
    if (!terrainMeshReady_) return;

    for (RenderInstance& inst : sceneInstances_) {
        // The player owns its own grounding, through PlayerController.
        if (inst.isPlayer) continue;
        // Same sampler the player walks on, so nothing ends up on a different surface.
        const float ground = terrainMesh_.sampleHeight(inst.position.x, inst.position.z);
        inst.position.y += ground + inst.scale.y;
    }
}

void Renderer::syncPhysicsToInstances()
{
    if (bodyToEntity_.empty()) return;

    for (const auto& [bodyId, entityId] : bodyToEntity_) {
        const dash::physics::Vec3 p = physicsWorld_.position(bodyId);
        for (auto& inst : sceneInstances_) {
            if (inst.entityId != entityId) continue;
            inst.position = p;
            break;
        }
    }
}

void Renderer::recordDrawCommands(VkCommandBuffer cmd, uint32_t imageIndex)
{
    // Resolved up front because the depth pass, recorded before the main render
    // pass, draws the very same casters with the very same bone palettes.
    std::vector<InstanceResources> instanceRes(sceneInstances_.size());
    for (size_t i = 0; i < sceneInstances_.size(); ++i) {
        if (i < sceneInstanceMeshes_.size()) instanceRes[i].mesh = sceneInstanceMeshes_[i];

        const int matIdx = (i < sceneInstanceMaterials_.size()) ? sceneInstanceMaterials_[i] : -1;
        if (matIdx < 0) continue;

        const MaterialGpu& mat = materials_[static_cast<size_t>(matIdx)];
        if (!mat.sets.empty()) instanceRes[i].materialSet = mat.sets[imageIndex];
        for (int c = 0; c < 3; ++c) instanceRes[i].tint[c] = mat.asset.baseColor[c];
        instanceRes[i].metallic = mat.asset.metallic;
        instanceRes[i].roughness = mat.asset.roughness;
    }

    updateSkinnedInstances(frameDeltaSeconds_, instanceRes);

    recordShadowPass(cmd, imageIndex, instanceRes);

    // Sky/clear colour, authored as a display value and linearised here so it
    // enters the HDR target in the same space the shaders write.
    const float clearColor[4] = {0.478f, 0.639f, 0.891f, 1.0f};
    hdr_.beginPass(cmd, clearColor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      texturedPipeline_ != VK_NULL_HANDLE ? texturedPipeline_ : pipeline_);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
        0, 1, &descriptorSets_[imageIndex], 0, nullptr);

    VkBuffer vertexBuffers[] = { meshBuffers_.vertexBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, meshBuffers_.indexBuffer(), 0, meshBuffers_.indexType());

    // Physics cube (standalone mode only)
    if (!editorBridge_.isEmbeddedPreview()) {
        const auto& lt = editorBridge_.lighting();
        const Mat4 model = trs(
            {cubeTransform_.position.x * TILE_SCALE,
             cubeTransform_.position.y,
             cubeTransform_.position.z * TILE_SCALE},
            0.0f, 0.0f, 0.0f,
            {0.30f, 0.30f, 0.30f});
        float pc[kInstancePushConstantFloats];
        buildInstancePushConstants(model, 0.86f, 0.34f, 0.34f, 1.0f, lt, pc);
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), pc);
        vkCmdDrawIndexed(cmd, meshBuffers_.indexCount(), 1, 0, 0, 0);
    }

    // Fallback tile instances (used when no terrain mesh pipeline)
    for (const auto& tile : terrainInstances_) {
        const auto& lt2 = editorBridge_.lighting();
        const Mat4 model = trs(
            {tile.position.x * TILE_SCALE, tile.position.y, tile.position.z * TILE_SCALE},
            tile.yawDeg, tile.pitchDeg, tile.rollDeg,
            {tile.scale.x, tile.scale.y, tile.scale.z});
        float pc[kInstancePushConstantFloats];
        buildInstancePushConstants(model, tile.color.x, tile.color.y, tile.color.z, 1.0f, lt2, pc);
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), pc);
        vkCmdDrawIndexed(cmd, meshBuffers_.indexCount(), 1, 0, 0, 0);
    }

    // ── Terrain heightmap mesh (single draw call) ─────────────────────
    if (terrainPipeline_ != VK_NULL_HANDLE && terrainMeshBuffers_.indexCount() > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeline_);
        vkCmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipelineLayout_,
            0, 1, &descriptorSets_[imageIndex], 0, nullptr);

        VkBuffer terrainVB[] = { terrainMeshBuffers_.vertexBuffer() };
        VkDeviceSize terrainOffsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, terrainVB, terrainOffsets);
        vkCmdBindIndexBuffer(cmd, terrainMeshBuffers_.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        const auto& lt = editorBridge_.lighting();
        const auto& fg = editorBridge_.fog();
        float layerRoughness[kTerrainRoughnessFloats];
        packTerrainLayerRoughness(layerRoughness);
        float terrainPC[kTerrainPushConstantFloats] = {
            camera_.x(), camera_.y(), camera_.z(), elapsedSeconds_,
            fg.start, fg.end, lt.dirX, lt.dirY,
            lt.dirZ, lt.intensity, lt.colorR, lt.colorG,
            lt.colorB, lt.ambient, 0.0f, 0.0f
        };
        std::copy(std::begin(layerRoughness), std::end(layerRoughness), terrainPC + 16);
        vkCmdPushConstants(cmd, terrainPipelineLayout_,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(terrainPC), terrainPC);

        vkCmdDrawIndexed(cmd, terrainMeshBuffers_.indexCount(), 1, 0, 0, 0);

        // ── Water mesh (translucent, after terrain) ──────────────
        if (waterPipeline_ != VK_NULL_HANDLE && waterMeshBuffers_.indexCount() > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipeline_);
            vkCmdBindDescriptorSets(
                cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipelineLayout_,
                0, 1, &descriptorSets_[imageIndex], 0, nullptr);

            VkBuffer waterVB[] = { waterMeshBuffers_.vertexBuffer() };
            vkCmdBindVertexBuffers(cmd, 0, 1, waterVB, terrainOffsets);
            vkCmdBindIndexBuffer(cmd, waterMeshBuffers_.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            vkCmdPushConstants(cmd, waterPipelineLayout_,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(terrainPC), terrainPC);

            vkCmdDrawIndexed(cmd, waterMeshBuffers_.indexCount(), 1, 0, 0, 0);
        }

        // Re-bind basic pipeline for scene entities
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          texturedPipeline_ != VK_NULL_HANDLE ? texturedPipeline_ : pipeline_);
        vkCmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
            0, 1, &descriptorSets_[imageIndex], 0, nullptr);
        VkBuffer cubeVB[] = { meshBuffers_.vertexBuffer() };
        vkCmdBindVertexBuffers(cmd, 0, 1, cubeVB, terrainOffsets);
        vkCmdBindIndexBuffer(cmd, meshBuffers_.indexBuffer(), 0, meshBuffers_.indexType());
    }

    // Scene entity instances
    const float aspect = static_cast<float>(swapchain_.extent().width)
                       / static_cast<float>(swapchain_.extent().height);

    SceneDrawParams drawParams;
    drawParams.opaquePipeline    = texturedPipeline_ != VK_NULL_HANDLE ? texturedPipeline_ : pipeline_;
    drawParams.opaqueLayout      = pipelineLayout_;
    drawParams.billboardPipeline = billboardPipeline_;
    drawParams.billboardLayout   = billboardPipelineLayout_;
    drawParams.skinnedPipeline   = skinnedPipeline_;
    drawParams.skinnedLayout     = skinnedPipelineLayout_;
    drawParams.boneSet           = boneSet_;
    drawParams.bonePalette       = &bonePalette_;
    drawParams.defaultSet        = descriptorSets_[imageIndex];
    drawParams.fallbackMesh      = &meshBuffers_;
    drawParams.lights            = &sceneLights_;
    drawParams.viewProj          = camera_.computeViewProjection(aspect);
    drawParams.cameraRight       = camera_.rightVector();
    drawParams.cameraUp          = camera_.upVector();

    const SceneDrawStats stats = drawSceneInstances(
        cmd, sceneInstances_, instanceRes, editorBridge_.lighting(), drawParams);

    lastDrawnInstances_ = stats.drawn;
    lastCulledInstances_ = stats.culled;

    hdr_.endPass(cmd);

    // Resolve: exposure + ACES + grading into the swapchain image.
    VkClearValue resolveClears[2]{};
    resolveClears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo resolveBegin{};
    resolveBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    resolveBegin.renderPass = swapchain_.renderPass();
    resolveBegin.framebuffer = frameGraph_.framebuffer(imageIndex);
    resolveBegin.renderArea.offset = {0, 0};
    resolveBegin.renderArea.extent = swapchain_.extent();
    resolveBegin.clearValueCount = 2;
    resolveBegin.pClearValues = resolveClears;

    vkCmdBeginRenderPass(cmd, &resolveBegin, VK_SUBPASS_CONTENTS_INLINE);
    hdr_.drawTonemap(cmd, grading_, /*encodeSrgb=*/false);
    vkCmdEndRenderPass(cmd);
}

bool Renderer::runSmoke(WindowContext& window, uint32_t targetFrames)
{
    if (!initialized_) return false;

    const bool infiniteRun = (targetFrames == 0);

    frameMs_.clear();
    recordMs_.clear();
    if (!infiniteRun) {
        frameMs_.reserve(targetFrames);
        recordMs_.reserve(targetFrames);
    }

    uint32_t renderedFrames = 0;
    auto lastTime = std::chrono::steady_clock::now();

    while ((infiniteRun || renderedFrames < targetFrames) && !window.shouldClose()) {
        const auto frameT0 = std::chrono::steady_clock::now();
        window.pollEvents();

        if (infiniteRun) {
            editorBridge_.poll(window.handle(), camera_, cubeTransform_);
        }

        const auto now = std::chrono::steady_clock::now();
        const float rawDt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        // Play-mode transport: 0 while paused, one fixed frame per step request.
        const float dt = infiniteRun ? editorBridge_.applyPlaybackScale(rawDt) : rawDt;
        elapsedSeconds_ += dt;
        frameDeltaSeconds_ = dt;

        if (!(infiniteRun && editorBridge_.hasExternalSelection())) {
            fixedAccumulator_ += dt;
            static constexpr float kFixedDt = 1.0f / 60.0f;
            static constexpr int kMaxSubsteps = 4;
            int substeps = 0;
            while (fixedAccumulator_ >= kFixedDt && substeps < kMaxSubsteps) {
                physicsWorld_.step(kFixedDt);
                fixedAccumulator_ -= kFixedDt;
                ++substeps;
            }
            transformProxy_.syncFromPhysics(physicsWorld_, cubeBodyId_, cubeTransform_);
            syncPhysicsToInstances();
            events_.flush();
        }

        // Standalone startup helper: align camera to look at the loaded scene body
        camera_.applyAutoFocusIfPending();

        // ── Player movement with WASD (player-centric gameplay) ──────────────
        if (player_.isLoaded()) {
            player_.update(window.handle(), inputBindings_, dt,
                           &terrainMesh_, terrainMeshReady_,
                           terrainHeightMap_, terrainMapWidth_, terrainMapHeight_);
            player_.syncToInstances(sceneInstances_);

            // ── Camera follows player (centered isometric view) ────────────────
            camera_.followPlayer(player_.x(), player_.y(), player_.z());
        }
        // ── Mouse look with right-click (legacy mode) ──────────────────────
        camera_.updateMouseLook(window.handle(), inputBindings_);

        // Frame rendering (always execute regardless of input mode)
        uint32_t imageIndex = 0;
        if (!frameGraph_.beginFrame(imageIndex)) break;
        if (!updateCameraUbo(imageIndex)) break;
        updateShadowMatrix();
        updateSceneLightsUbo(imageIndex);

        VkCommandBuffer cmd = frameGraph_.commandBuffer(imageIndex);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) break;

        const auto recordT0 = std::chrono::steady_clock::now();
        recordDrawCommands(cmd, imageIndex);
        const auto recordT1 = std::chrono::steady_clock::now();

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) break;

        if (!frameGraph_.endFrame(imageIndex)) break;
        ++renderedFrames;

        if (!infiniteRun) {
            const auto frameT1 = std::chrono::steady_clock::now();
            frameMs_.push_back(std::chrono::duration<float, std::milli>(frameT1 - frameT0).count());
            recordMs_.push_back(std::chrono::duration<float, std::milli>(recordT1 - recordT0).count());
        }

        if (!infiniteRun && renderedFrames == targetFrames) {
            const dash::physics::Vec3 p = physicsWorld_.position(cubeBodyId_);
            std::printf("[D83] Baseline settled cube position: (%.3f, %.3f, %.3f)\n", p.x, p.y, p.z);
        }
    }

    vkDeviceWaitIdle(deviceContext_.device());
    if (infiniteRun) {
        if (editorBridge_.isEmbeddedPreview()) {
            std::printf("[D76] Embedded preview loop finished after %u frames.\n", renderedFrames);
        } else {
            std::printf("[D76] Standalone persistent loop finished after %u frames.\n", renderedFrames);
        }
        return true;
    }

    if (renderedFrames >= targetFrames) {
        std::printf("[D76] Smoke test: %u frames rendered successfully.\n", targetFrames);
        std::printf("[Culling] Last frame: %u drawn, %u culled (of %zu instances).\n",
                    lastDrawnInstances_, lastCulledInstances_, sceneInstances_.size());
        reportFrameStats(renderedFrames);
        return true;
    }

    std::fprintf(stderr, "[D76] Smoke test interrupted at frame %u.\n", renderedFrames);
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// reportFrameStats — baseline for the A2 benchmark. Wall-clock frame time is
// pinned by FIFO vsync, so the command-recording column is the one that moves
// when draw submission changes.
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::reportFrameStats(uint32_t renderedFrames) const
{
    if (frameMs_.empty()) return;

    // Discard the first frames: they carry swapchain and pipeline warm-up.
    const size_t warmup = std::min<size_t>(frameMs_.size() / 10, 10);

    auto summarize = [warmup](std::vector<float> v, const char* label) {
        v.erase(v.begin(), v.begin() + static_cast<long>(warmup));
        if (v.empty()) return;
        std::sort(v.begin(), v.end());
        const double sum = std::accumulate(v.begin(), v.end(), 0.0);
        const size_t p95 = std::min(v.size() - 1,
                                    static_cast<size_t>(std::ceil(v.size() * 0.95)) - 1);
        std::printf("[Bench] %-16s min %7.3f  avg %7.3f  p95 %7.3f  max %7.3f ms\n",
                    label, v.front(), sum / static_cast<double>(v.size()), v[p95], v.back());
    };

    std::printf("[Bench] Samples: %u frames (%zu discarded as warm-up), FIFO vsync.\n",
                renderedFrames, warmup);
    summarize(frameMs_, "frame total");
    summarize(recordMs_, "cmd recording");
}

void Renderer::shutdown()
{
    if (!initialized_ && instance_ == VK_NULL_HANDLE) return;

    if (deviceContext_.device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(deviceContext_.device());
    }

    frameGraph_.shutdown();

    hdr_.shutdown(deviceContext_.device());
    shadowMap_.shutdown(deviceContext_.device());
    destroyBoneResources();
    destroySceneMaterials();

    for (size_t i = 0; i < uniformBuffers_.size(); ++i) {
        if (uniformBuffers_[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(deviceContext_.device(), uniformBuffers_[i], nullptr);
        }
        if (uniformMemories_[i] != VK_NULL_HANDLE) {
            vkFreeMemory(deviceContext_.device(), uniformMemories_[i], nullptr);
        }
    }
    uniformBuffers_.clear();
    uniformMemories_.clear();

    for (size_t i = 0; i < lightBuffers_.size(); ++i) {
        if (lightBuffers_[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(deviceContext_.device(), lightBuffers_[i], nullptr);
        }
        if (lightMemories_[i] != VK_NULL_HANDLE) {
            vkFreeMemory(deviceContext_.device(), lightMemories_[i], nullptr);
        }
    }
    lightBuffers_.clear();
    lightMemories_.clear();
    descriptorSets_.clear();

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(deviceContext_.device(), descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(deviceContext_.device(), descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }

    meshBuffers_.shutdown(deviceContext_.device());
    terrainMeshBuffers_.shutdown(deviceContext_.device());
    waterMeshBuffers_.shutdown(deviceContext_.device());

    PipelineBuilder::destroy(deviceContext_.device(), pipelineLayout_, pipeline_);
    pipelineLayout_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;

    PipelineBuilder::destroy(deviceContext_.device(), texturedPipelineLayout_, texturedPipeline_);
    texturedPipelineLayout_ = VK_NULL_HANDLE;
    texturedPipeline_ = VK_NULL_HANDLE;

    PipelineBuilder::destroy(deviceContext_.device(), terrainPipelineLayout_, terrainPipeline_);
    terrainPipelineLayout_ = VK_NULL_HANDLE;
    terrainPipeline_ = VK_NULL_HANDLE;

    PipelineBuilder::destroy(deviceContext_.device(), waterPipelineLayout_, waterPipeline_);
    waterPipelineLayout_ = VK_NULL_HANDLE;
    waterPipeline_ = VK_NULL_HANDLE;

    PipelineBuilder::destroy(deviceContext_.device(), billboardPipelineLayout_, billboardPipeline_);
    billboardPipelineLayout_ = VK_NULL_HANDLE;
    billboardPipeline_ = VK_NULL_HANDLE;

    TextureLoader::destroy(deviceContext_.device(), defaultTexture_);
    destroyTerrainTextureSet(deviceContext_.device(), terrainTextures_);

    assetCache_.clear(deviceContext_.device());

    swapchain_.shutdown(deviceContext_.device());
    deviceContext_.shutdown();

    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    initialized_ = false;
}

} // namespace dash::vkexp
