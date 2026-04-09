#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "rendering/platform/WindowContext.h"
#include "rendering/vulkan/DeviceContext.h"
#include "rendering/vulkan/PipelineBuilder.h"
#include "rendering/vulkan/SwapchainContext.h"
#include "rendering/vulkan/VulkanDiagnostics.h"

#ifndef VULKAN_SHADER_DIR
#define VULKAN_SHADER_DIR ""
#endif

namespace {

bool hasValidationLayer()
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

VkInstance createInstance(const std::vector<const char*>& requiredExtensions)
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Dash Vulkan Bootstrap";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "Dash-Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    std::set<std::string> extensionsSet;
    for (const char* ext : requiredExtensions) {
        if (ext) extensionsSet.emplace(ext);
    }

    uint32_t availableExtCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, nullptr);
    std::vector<VkExtensionProperties> availableExts(availableExtCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, availableExts.data());

    bool hasPortabilityEnumeration = false;
    for (const auto& ext : availableExts) {
        if (std::string(ext.extensionName) == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) {
            hasPortabilityEnumeration = true;
            break;
        }
    }
    if (hasPortabilityEnumeration) {
        extensionsSet.emplace(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    }

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

    VkInstance instance = VK_NULL_HANDLE;
    const VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[D70] vkCreateInstance returned %d\n", static_cast<int>(result));
        return VK_NULL_HANDLE;
    }
    return instance;
}

} // namespace

int main()
{
    std::printf("%s", dash::vkexp::VulkanDiagnostics::buildReport().c_str());

    dash::vkexp::WindowContext window;
    if (!window.init(1280, 720, "Dash Vulkan Bootstrap (D70-D74)")) {
        return 1;
    }

    VkInstance instance = createInstance(window.requiredVulkanExtensions());
    if (instance == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[D70] vkCreateInstance failed.\n");
        return 1;
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!window.createSurface(instance, surface)) {
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    dash::vkexp::DeviceContext deviceContext;
    if (!deviceContext.init(instance, surface)) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    dash::vkexp::SwapchainContext swapchain;
    if (!swapchain.init(deviceContext, surface, window.handle())) {
        deviceContext.shutdown();
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    const std::string vert = std::string(VULKAN_SHADER_DIR) + "/basic.vert.spv";
    const std::string frag = std::string(VULKAN_SHADER_DIR) + "/basic.frag.spv";

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    std::string pipelineError;
    if (!dash::vkexp::PipelineBuilder::createBasicPipeline(
            deviceContext.device(),
            swapchain.extent(),
            swapchain.renderPass(),
            vert,
            frag,
            pipelineLayout,
            pipeline,
            pipelineError)) {
        std::fprintf(stderr, "[D74] Pipeline creation failed: %s\n", pipelineError.c_str());
        swapchain.shutdown(deviceContext.device());
        deviceContext.shutdown();
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    std::puts("[D70-D74] Vulkan bootstrap completed successfully.");

    for (int i = 0; i < 120 && !window.shouldClose(); ++i) {
        window.pollEvents();
    }

    dash::vkexp::PipelineBuilder::destroy(deviceContext.device(), pipelineLayout, pipeline);
    swapchain.shutdown(deviceContext.device());
    deviceContext.shutdown();
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
}
