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
#include <set>
#include <unordered_map>

#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

#include "game/physics/DebugPhysicsDraw.h"
#include "rendering/Frustum.h"
#include "rendering/vulkan/PipelineBuilder.h"
#include "rendering/mesh/TerrainVertex.h"
#include "world/TerrainMesh.h"

#ifndef VULKAN_SHADER_DIR
#define VULKAN_SHADER_DIR ""
#endif

namespace dash::vkexp {

using json = nlohmann::json;

namespace {

static constexpr const char* kGetPhysicalDeviceProps2Ext = "VK_KHR_get_physical_device_properties2";

// Push constant block shared by the basic/textured pipelines:
// mat4 model (16 floats) + color/alpha (4) + lightDir/intensity (4).
constexpr size_t kInstancePushConstantFloats = 24;

void buildInstancePushConstants(const Mat4& model,
                                float r, float g, float b, float a,
                                const LightingParams& light,
                                float (&out)[kInstancePushConstantFloats])
{
    std::memcpy(out, model.m, sizeof(model.m));
    out[16] = r;
    out[17] = g;
    out[18] = b;
    out[19] = a;
    out[20] = light.dirX;
    out[21] = light.dirY;
    out[22] = light.dirZ;
    out[23] = light.intensity;
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

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboBinding, samplerBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(deviceContext_.device(), &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] Failed to create descriptor set layout.\n");
        return false;
    }

    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, swapchain_.imageCount()},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, swapchain_.imageCount()}
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

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers_[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(CameraUBO);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = defaultTexture_.imageView;
        imageInfo.sampler = defaultTexture_.sampler;

        std::array<VkWriteDescriptorSet, 2> writes{};

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

        vkUpdateDescriptorSets(deviceContext_.device(),
                               static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }

    return true;
}

bool Renderer::createPipeline()
{
    const std::string vert = std::string(VULKAN_SHADER_DIR) + "/basic.vert.spv";
    const std::string frag = std::string(VULKAN_SHADER_DIR) + "/basic.frag.spv";

    std::string pipelineError;
    if (!PipelineBuilder::createBasicPipeline(
            deviceContext_.device(),
            swapchain_.extent(),
            swapchain_.renderPass(),
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
    const std::string texFrag = std::string(VULKAN_SHADER_DIR) + "/textured.frag.spv";

    std::string texPipelineError;
    if (!PipelineBuilder::createBasicPipeline(
            deviceContext_.device(),
            swapchain_.extent(),
            swapchain_.renderPass(),
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
    const std::string terrainFrag = std::string(VULKAN_SHADER_DIR) + "/terrain.frag.spv";

    std::string terrainPipelineError;
    if (!PipelineBuilder::createTerrainPipeline(
            deviceContext_.device(),
            swapchain_.extent(),
            swapchain_.renderPass(),
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
            swapchain_.renderPass(),
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
            swapchain_.renderPass(),
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
    if (!swapchain_.init(deviceContext_, surface_, window.handle())) return false;

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

        spawnSceneryPhysicsBodies(loadedScene);
    }

    if (sceneInstances_.empty()) {
        sceneInstances_.push_back({spawn, {0.26f, 0.52f, 0.26f}, {0.30f, 0.58f, 0.95f}});
    }

    resolveSceneMeshes();
    resolveSceneMaterials();
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

// Resolves a mesh id to GPU buffers, loading and caching it on first use.
// Returns nullptr to signal "use the builtin cube".
const MeshBuffers* Renderer::resolveMesh(const std::string& meshId)
{
    if (meshId.empty() || meshId == "cube") return nullptr;

    if (CachedModel* cached = assetCache_.get(meshId)) {
        return cached->meshBuffers.indexCount() > 0 ? &cached->meshBuffers : nullptr;
    }

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

    fs::path resolved;
    for (const auto& c : candidates) {
        if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) { resolved = c; break; }
        ec.clear();
    }

    CachedModel model;
    if (resolved.empty()) {
        std::fprintf(stderr, "[AssetCache3D] Mesh not found: '%s' (using builtin cube)\n",
                     meshId.c_str());
    } else if (!model.meshBuffers.initFromGLTF(deviceContext_.physicalDevice(),
                                               deviceContext_.device(),
                                               resolved.string())) {
        std::fprintf(stderr, "[AssetCache3D] Failed to load mesh: %s (using builtin cube)\n",
                     resolved.string().c_str());
        model.meshBuffers.shutdown(deviceContext_.device());
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

void Renderer::destroySceneMaterials()
{
    VkDevice dev = deviceContext_.device();
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
        if (raw.is_absolute()) {
            candidates.push_back(raw);
        } else {
            if (!scenePath_.empty())
                candidates.push_back(fs::path(scenePath_).parent_path() / raw);
            candidates.push_back(fs::path(VULKAN_MODEL_DIR) / raw);
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

    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, setCount},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, setCount}
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

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = tex.imageView;
            imageInfo.sampler = tex.sampler;

            std::array<VkWriteDescriptorSet, 2> writes{};
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
    VkClearValue clearValues[2]{};
    clearValues[0].color = { {0.72f, 0.82f, 0.95f, 1.0f} };
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = swapchain_.renderPass();
    rpBegin.framebuffer = frameGraph_.framebuffer(imageIndex);
    rpBegin.renderArea.offset = {0, 0};
    rpBegin.renderArea.extent = swapchain_.extent();
    rpBegin.clearValueCount = 2;
    rpBegin.pClearValues = clearValues;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
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
        const float terrainPC[16] = {
            camera_.x(), camera_.y(), camera_.z(), elapsedSeconds_,
            fg.start, fg.end, lt.dirX, lt.dirY,
            lt.dirZ, lt.intensity, lt.colorR, lt.colorG,
            lt.colorB, lt.ambient, lt.specStr, lt.specShin
        };
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
    const Mat4 viewProj = camera_.computeViewProjection(aspect);
    const dash::Frustum frustum = dash::Frustum::fromViewProj(viewProj.m);

    const MeshBuffers* boundMesh = &meshBuffers_;
    int boundMaterial = -1;
    bool hasBillboards = false;
    uint32_t drawn = 0, culled = 0;
    for (size_t i = 0; i < sceneInstances_.size(); ++i) {
        const auto& instance = sceneInstances_[i];
        if (!instance.visible) continue;
        if (instance.renderMode == static_cast<int>(InstanceRenderMode::BillboardSprite)) {
            hasBillboards = true;
            continue;  // drawn in the transparent pass below
        }

        if (!frustum.intersectsAabb(instance.position.x * TILE_SCALE,
                                    instance.position.y,
                                    instance.position.z * TILE_SCALE,
                                    instance.scale.x, instance.scale.y, instance.scale.z)) {
            ++culled;
            continue;
        }
        ++drawn;

        const MeshBuffers* mesh = (i < sceneInstanceMeshes_.size() && sceneInstanceMeshes_[i])
                                ? sceneInstanceMeshes_[i]
                                : &meshBuffers_;
        if (mesh != boundMesh) {
            VkBuffer vb[] = { mesh->vertexBuffer() };
            VkDeviceSize vbOffsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vb, vbOffsets);
            vkCmdBindIndexBuffer(cmd, mesh->indexBuffer(), 0, mesh->indexType());
            boundMesh = mesh;
        }

        const int matIdx = (i < sceneInstanceMaterials_.size()) ? sceneInstanceMaterials_[i] : -1;
        if (matIdx != boundMaterial) {
            VkDescriptorSet set = (matIdx >= 0 && !materials_[static_cast<size_t>(matIdx)].sets.empty())
                                ? materials_[static_cast<size_t>(matIdx)].sets[imageIndex]
                                : descriptorSets_[imageIndex];
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                                    0, 1, &set, 0, nullptr);
            boundMaterial = matIdx;
        }

        float cr = instance.color.x, cg = instance.color.y, cb = instance.color.z;
        if (matIdx >= 0) {
            const auto& base = materials_[static_cast<size_t>(matIdx)].asset.baseColor;
            cr *= base[0]; cg *= base[1]; cb *= base[2];
        }

        const auto& lt3 = editorBridge_.lighting();
        const Mat4 model = trs(
            {instance.position.x * TILE_SCALE, instance.position.y, instance.position.z * TILE_SCALE},
            instance.yawDeg, instance.pitchDeg, instance.rollDeg,
            {instance.scale.x, instance.scale.y, instance.scale.z});
        float pc[kInstancePushConstantFloats];
        buildInstancePushConstants(model, cr, cg, cb, 1.0f, lt3, pc);
        vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), pc);
        vkCmdDrawIndexed(cmd, mesh->indexCount(), 1, 0, 0, 0);
    }

    // Leave the default set bound for the next frame's terrain/tile passes.
    if (boundMaterial != -1) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_,
                                0, 1, &descriptorSets_[imageIndex], 0, nullptr);
    }

    // ── Billboard sprites (transparent, drawn after the opaque pass) ─────
    if (hasBillboards && billboardPipeline_ != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, billboardPipeline_);

        const Vec3 camRight = camera_.rightVector();
        const Vec3 camUp = camera_.upVector();
        int boundBillboardMaterial = -2;

        for (size_t i = 0; i < sceneInstances_.size(); ++i) {
            const auto& instance = sceneInstances_[i];
            if (!instance.visible) continue;
            if (instance.renderMode != static_cast<int>(InstanceRenderMode::BillboardSprite)) continue;

            if (!frustum.intersectsAabb(instance.position.x * TILE_SCALE,
                                        instance.position.y,
                                        instance.position.z * TILE_SCALE,
                                        instance.scale.x, instance.scale.y, instance.scale.x)) {
                ++culled;
                continue;
            }
            ++drawn;

            const int matIdx = (i < sceneInstanceMaterials_.size()) ? sceneInstanceMaterials_[i] : -1;
            if (matIdx != boundBillboardMaterial) {
                VkDescriptorSet set = (matIdx >= 0 && !materials_[static_cast<size_t>(matIdx)].sets.empty())
                                    ? materials_[static_cast<size_t>(matIdx)].sets[imageIndex]
                                    : descriptorSets_[imageIndex];
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, billboardPipelineLayout_,
                                        0, 1, &set, 0, nullptr);
                boundBillboardMaterial = matIdx;
            }

            float cr = instance.color.x, cg = instance.color.y, cb = instance.color.z;
            if (matIdx >= 0) {
                const auto& base = materials_[static_cast<size_t>(matIdx)].asset.baseColor;
                cr *= base[0]; cg *= base[1]; cb *= base[2];
            }

            const float pc[20] = {
                instance.position.x * TILE_SCALE, instance.position.y, instance.position.z * TILE_SCALE, 0.0f,
                instance.scale.x, instance.scale.y, 0.0f, 0.0f,
                cr, cg, cb, 1.0f,
                camRight.x, camRight.y, camRight.z, 0.0f,
                camUp.x, camUp.y, camUp.z, 0.0f
            };
            vkCmdPushConstants(cmd, billboardPipelineLayout_,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), pc);
            vkCmdDraw(cmd, 6, 1, 0, 0);
        }
    }

    lastDrawnInstances_ = drawn;
    lastCulledInstances_ = culled;

    vkCmdEndRenderPass(cmd);
}

bool Renderer::runSmoke(WindowContext& window, uint32_t targetFrames)
{
    if (!initialized_) return false;

    const bool infiniteRun = (targetFrames == 0);

    uint32_t renderedFrames = 0;
    auto lastTime = std::chrono::steady_clock::now();

    while ((infiniteRun || renderedFrames < targetFrames) && !window.shouldClose()) {
        window.pollEvents();

        if (infiniteRun) {
            editorBridge_.poll(window.handle(), camera_, cubeTransform_);
        }

        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        elapsedSeconds_ += dt;

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

        VkCommandBuffer cmd = frameGraph_.commandBuffer(imageIndex);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) break;

        recordDrawCommands(cmd, imageIndex);

        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) break;

        if (!frameGraph_.endFrame(imageIndex)) break;
        ++renderedFrames;

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
        return true;
    }

    std::fprintf(stderr, "[D76] Smoke test interrupted at frame %u.\n", renderedFrames);
    return false;
}

void Renderer::shutdown()
{
    if (!initialized_ && instance_ == VK_NULL_HANDLE) return;

    if (deviceContext_.device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(deviceContext_.device());
    }

    frameGraph_.shutdown();

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
