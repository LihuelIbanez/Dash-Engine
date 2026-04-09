#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "rendering/mesh/MeshBuffers.h"
#include "rendering/platform/WindowContext.h"
#include "rendering/vulkan/DeviceContext.h"
#include "rendering/vulkan/FrameGraphLite.h"
#include "rendering/vulkan/SwapchainContext.h"

namespace dash::vkexp {

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool init(WindowContext& window);
    bool runSmoke(WindowContext& window, uint32_t targetFrames);
    void shutdown();

private:
    bool createInstance(const std::vector<const char*>& requiredExtensions);
    bool createDescriptors();
    bool createPipeline();
    bool createPerFrameUniformBuffers();
    bool updateCameraUbo(uint32_t imageIndex);

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    DeviceContext deviceContext_;
    SwapchainContext swapchain_;
    MeshBuffers meshBuffers_;
    FrameGraphLite frameGraph_;

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;
    std::vector<VkBuffer> uniformBuffers_;
    std::vector<VkDeviceMemory> uniformMemories_;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    bool initialized_ = false;

    float cameraX_ = 0.0f;
    float cameraY_ = 0.0f;
    float cameraZ_ = 2.2f;
    float yawDegrees_ = -90.0f;
    float pitchDegrees_ = 0.0f;
    bool hadLookFrame_ = false;
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
};

} // namespace dash::vkexp
