#include "rendering/vulkan/Renderer.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>

#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

#include "game/physics/DebugPhysicsDraw.h"
#include "rendering/vulkan/PipelineBuilder.h"

#ifndef VULKAN_SHADER_DIR
#define VULKAN_SHADER_DIR ""
#endif

namespace dash::vkexp {

using json = nlohmann::json;

namespace {

static constexpr const char* kGetPhysicalDeviceProps2Ext = "VK_KHR_get_physical_device_properties2";

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Mat4 {
    float m[16]{};
};

struct CameraUBO {
    Mat4 mvp;
};

static Mat4 identity()
{
    Mat4 out{};
    out.m[0] = 1.0f;
    out.m[5] = 1.0f;
    out.m[10] = 1.0f;
    out.m[15] = 1.0f;
    return out;
}

static Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 out{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

static float dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static Vec3 normalize(const Vec3& v)
{
    const float len = std::sqrt(dot(v, v));
    if (len <= 0.00001f) return {0.0f, 0.0f, 0.0f};
    return {v.x / len, v.y / len, v.z / len};
}

static Mat4 perspective(float fovYRadians, float aspect, float zNear, float zFar)
{
    Mat4 out{};
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    out.m[0] = f / aspect;
    out.m[5] = f;
    out.m[10] = zFar / (zNear - zFar);
    out.m[11] = -1.0f;
    out.m[14] = (zFar * zNear) / (zNear - zFar);
    return out;
}

static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up)
{
    const Vec3 f = normalize({center.x - eye.x, center.y - eye.y, center.z - eye.z});
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);

    Mat4 out = identity();
    out.m[0] = s.x;
    out.m[1] = u.x;
    out.m[2] = -f.x;
    out.m[4] = s.y;
    out.m[5] = u.y;
    out.m[6] = -f.y;
    out.m[8] = s.z;
    out.m[9] = u.z;
    out.m[10] = -f.z;
    out.m[12] = -dot(s, eye);
    out.m[13] = -dot(u, eye);
    out.m[14] = dot(f, eye);
    return out;
}

static Mat4 translate(const Vec3& t)
{
    Mat4 out = identity();
    out.m[12] = t.x;
    out.m[13] = t.y;
    out.m[14] = t.z;
    return out;
}

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            ((memProperties.memoryTypes[i].propertyFlags & properties) == properties)) {
            return i;
        }
    }
    return UINT32_MAX;
}

static bool createHostVisibleBuffer(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer& outBuffer,
    VkDeviceMemory& outMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, outBuffer, &req);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        physicalDevice,
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(device, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }

    if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(device, outBuffer, outMemory, 0);
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

void Renderer::setEditorStatePath(const std::string& statePath)
{
    editorStatePath_ = statePath;
}

void Renderer::setEmbeddedPreview(bool enabled)
{
    embeddedPreview_ = enabled;
}

void Renderer::applyEditorStateIfNeeded(GLFWwindow* window)
{
    if (editorStatePath_.empty()) return;

    const auto now = std::chrono::steady_clock::now();
    if (lastEditorStateRead_.time_since_epoch().count() != 0) {
        const auto deltaMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastEditorStateRead_).count();
        if (deltaMs < 16) return;
    }
    lastEditorStateRead_ = now;

    std::ifstream in(editorStatePath_);
    if (!in.is_open()) return;

    json j;
    try {
        in >> j;
    } catch (...) {
        return;
    }

    if (j.contains("camera") && j["camera"].is_object()) {
        const auto& c = j["camera"];
        cameraX_ = c.value("x", cameraX_);
        cameraY_ = c.value("y", cameraY_);
        yawDegrees_ = c.value("isoYawDeg", yawDegrees_);
        pitchDegrees_ = c.value("isoPitchDeg", pitchDegrees_);
        if (pitchDegrees_ > 89.0f) pitchDegrees_ = 89.0f;
        if (pitchDegrees_ < -89.0f) pitchDegrees_ = -89.0f;
    }

    hasExternalSelection_ = false;
    if (j.contains("selection") && j["selection"].is_object()) {
        const auto& s = j["selection"];
        const uint64_t entityId = s.value("entityId", static_cast<uint64_t>(0));
        if (entityId != 0) {
            cubeTransform_.position.x = s.value("x", cubeTransform_.position.x);
            cubeTransform_.position.y = s.value("y", cubeTransform_.position.y);
            cubeTransform_.position.z = s.value("z", cubeTransform_.position.z);
            hasExternalSelection_ = true;
        }
    }

    if (embeddedPreview_ && window && j.contains("viewport") && j["viewport"].is_object()) {
        const auto& vp = j["viewport"];
        const int sx = static_cast<int>(vp.value("screenX", 0.0f));
        const int sy = static_cast<int>(vp.value("screenY", 0.0f));
        const int sw = std::max(64, static_cast<int>(vp.value("screenW", 640.0f)));
        const int sh = std::max(64, static_cast<int>(vp.value("screenH", 360.0f)));

        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_TRUE);
        glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_FALSE);
        glfwSetWindowPos(window, sx, sy);
        glfwSetWindowSize(window, sw, sh);
    }
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

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboBinding;
    if (vkCreateDescriptorSetLayout(deviceContext_.device(), &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] Failed to create descriptor set layout.\n");
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = swapchain_.imageCount();

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = swapchain_.imageCount();
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
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

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSets_[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(deviceContext_.device(), 1, &write, 0, nullptr);
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

    if (!createPerFrameUniformBuffers()) return false;

    if (!frameGraph_.init(
            deviceContext_.device(),
            deviceContext_.queueFamilies().graphicsFamily.value(),
            deviceContext_.graphicsQueue(),
            deviceContext_.presentQueue(),
            swapchain_.swapchain(),
            swapchain_.extent(),
            swapchain_.renderPass(),
            swapchain_.imageViews())) {
        return false;
    }

    if (!physicsWorld_.init()) {
        std::fprintf(stderr, "[D80] PhysicsWorld initialization failed.\n");
        return false;
    }
    physicsWorld_.setGravity({0.0f, -9.8f, 0.0f});
    physicsWorld_.setRestitution(0.20f);
    physicsWorld_.setCollisionCallback([](const dash::physics::CollisionEvent& ev) {
        if (ev.type == dash::physics::CollisionEventType::Enter) {
            std::printf("[D82] Collision Enter: %d <-> %d\n", ev.a, ev.b);
        }
    });

    floorBodyId_ = physicsWorld_.createStaticPlane(-0.7f);
    cubeBodyId_ = physicsWorld_.createDynamicBox({0.0f, 0.8f, 0.0f}, {0.30f, 0.30f, 0.30f}, 1.0f);
    transformProxy_.syncFromPhysics(physicsWorld_, cubeBodyId_, cubeTransform_);
    dash::physics::DebugPhysicsDraw::logBodyAabb(physicsWorld_, floorBodyId_, "floor");
    dash::physics::DebugPhysicsDraw::logBodyAabb(physicsWorld_, cubeBodyId_, "cube");

    initialized_ = true;
    std::puts("[D78] Renderer + FrameGraphLite initialized.");
    std::puts("[D80-D83] PhysicsWorld active (fixed-step + cube/plane baseline).");
    return true;
}

bool Renderer::updateCameraUbo(uint32_t imageIndex)
{
    auto forwardFromAngles = [&]() -> Vec3 {
        const float yaw = yawDegrees_ * 0.0174532925f;
        const float pitch = pitchDegrees_ * 0.0174532925f;
        return normalize({
            std::cos(yaw) * std::cos(pitch),
            std::sin(pitch),
            std::sin(yaw) * std::cos(pitch)
        });
    };

    Vec3 forward = forwardFromAngles();
    const Vec3 target{ cameraX_ + forward.x, cameraY_ + forward.y, cameraZ_ + forward.z };

    Mat4 model = translate({cubeTransform_.position.x, cubeTransform_.position.y, cubeTransform_.position.z});
    Mat4 view = lookAt({cameraX_, cameraY_, cameraZ_}, target, {0.0f, 1.0f, 0.0f});
    Mat4 proj = perspective(
        60.0f * 0.0174532925f,
        static_cast<float>(swapchain_.extent().width) / static_cast<float>(swapchain_.extent().height),
        0.1f,
        100.0f);
    proj.m[5] *= -1.0f;

    CameraUBO ubo{};
    ubo.mvp = multiply(multiply(proj, view), model);

    void* mapped = nullptr;
    if (vkMapMemory(deviceContext_.device(), uniformMemories_[imageIndex], 0, sizeof(CameraUBO), 0, &mapped) != VK_SUCCESS) {
        return false;
    }
    std::memcpy(mapped, &ubo, sizeof(CameraUBO));
    vkUnmapMemory(deviceContext_.device(), uniformMemories_[imageIndex]);
    return true;
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
            applyEditorStateIfNeeded(window.handle());
        }

        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        if (!(infiniteRun && hasExternalSelection_)) {
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
        }

        auto forwardFromAngles = [&]() -> Vec3 {
            const float yaw = yawDegrees_ * 0.0174532925f;
            const float pitch = pitchDegrees_ * 0.0174532925f;
            return normalize({
                std::cos(yaw) * std::cos(pitch),
                std::sin(pitch),
                std::sin(yaw) * std::cos(pitch)
            });
        };

        Vec3 forward = forwardFromAngles();
        Vec3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));

        if (!infiniteRun) {
            const float moveSpeed = 1.9f;
            if (glfwGetKey(window.handle(), GLFW_KEY_W) == GLFW_PRESS) {
                cameraX_ += forward.x * moveSpeed * dt;
                cameraY_ += forward.y * moveSpeed * dt;
                cameraZ_ += forward.z * moveSpeed * dt;
            }
            if (glfwGetKey(window.handle(), GLFW_KEY_S) == GLFW_PRESS) {
                cameraX_ -= forward.x * moveSpeed * dt;
                cameraY_ -= forward.y * moveSpeed * dt;
                cameraZ_ -= forward.z * moveSpeed * dt;
            }
            if (glfwGetKey(window.handle(), GLFW_KEY_A) == GLFW_PRESS) {
                cameraX_ -= right.x * moveSpeed * dt;
                cameraY_ -= right.y * moveSpeed * dt;
                cameraZ_ -= right.z * moveSpeed * dt;
            }
            if (glfwGetKey(window.handle(), GLFW_KEY_D) == GLFW_PRESS) {
                cameraX_ += right.x * moveSpeed * dt;
                cameraY_ += right.y * moveSpeed * dt;
                cameraZ_ += right.z * moveSpeed * dt;
            }

            if (glfwGetMouseButton(window.handle(), GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
                double mouseX = 0.0;
                double mouseY = 0.0;
                glfwGetCursorPos(window.handle(), &mouseX, &mouseY);
                if (!hadLookFrame_) {
                    hadLookFrame_ = true;
                    lastMouseX_ = mouseX;
                    lastMouseY_ = mouseY;
                }
                const float dx = static_cast<float>(mouseX - lastMouseX_);
                const float dy = static_cast<float>(mouseY - lastMouseY_);
                lastMouseX_ = mouseX;
                lastMouseY_ = mouseY;

                const float sensitivity = 0.10f;
                yawDegrees_ += dx * sensitivity;
                pitchDegrees_ -= dy * sensitivity;
                if (pitchDegrees_ > 89.0f) pitchDegrees_ = 89.0f;
                if (pitchDegrees_ < -89.0f) pitchDegrees_ = -89.0f;
            } else {
                hadLookFrame_ = false;
            }
        }

        uint32_t imageIndex = 0;
        if (!frameGraph_.beginFrame(imageIndex)) break;
        if (!updateCameraUbo(imageIndex)) break;

        VkCommandBuffer cmd = frameGraph_.commandBuffer(imageIndex);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) break;

        VkClearValue clearColor{};
        clearColor.color = { {0.06f, 0.07f, 0.09f, 1.0f} };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = swapchain_.renderPass();
        rpBegin.framebuffer = frameGraph_.framebuffer(imageIndex);
        rpBegin.renderArea.offset = {0, 0};
        rpBegin.renderArea.extent = swapchain_.extent();
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearColor;

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout_,
            0,
            1,
            &descriptorSets_[imageIndex],
            0,
            nullptr);

        VkBuffer vertexBuffers[] = { meshBuffers_.vertexBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, meshBuffers_.indexBuffer(), 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexed(cmd, meshBuffers_.indexCount(), 1, 0, 0, 0);
        vkCmdEndRenderPass(cmd);
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
        std::printf("[D76] Editor preview loop finished after %u frames.\n", renderedFrames);
        return true;
    }

    if (renderedFrames >= targetFrames) {
        std::printf("[D76] Smoke test: %u frames rendered successfully.\n", targetFrames);
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
    PipelineBuilder::destroy(deviceContext_.device(), pipelineLayout_, pipeline_);
    pipelineLayout_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;

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
