#include "rendering/vulkan/DeviceContext.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace dash::vkexp {

static constexpr const char* kPortabilitySubsetExt = "VK_KHR_portability_subset";

DeviceContext::~DeviceContext()
{
    shutdown();
}

QueueFamilyIndices DeviceContext::findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) const
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport == VK_TRUE) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) break;
    }

    return indices;
}

bool DeviceContext::init(VkInstance instance, VkSurfaceKHR surface)
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        std::fprintf(stderr, "[D72] No Vulkan physical devices found.\n");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (VkPhysicalDevice candidate : devices) {
        QueueFamilyIndices qf = findQueueFamilies(candidate, surface);
        if (qf.isComplete()) {
            physicalDevice_ = candidate;
            queueFamilies_ = qf;
            break;
        }
    }

    if (physicalDevice_ == VK_NULL_HANDLE) {
        std::fprintf(stderr, "[D72] No suitable physical device (graphics+present) found.\n");
        return false;
    }

    std::set<uint32_t> uniqueQueueFamilies = {
        queueFamilies_.graphicsFamily.value(),
        queueFamilies_.presentFamily.value()
    };

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(uniqueQueueFamilies.size());

    for (uint32_t family : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueInfos.push_back(queueInfo);
    }

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availableExts(extCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice_, nullptr, &extCount, availableExts.data());

    bool hasPortabilitySubset = false;
    for (const auto& ext : availableExts) {
        if (std::string(ext.extensionName) == kPortabilitySubsetExt) {
            hasPortabilitySubset = true;
            break;
        }
    }

    std::vector<const char*> requiredExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    if (hasPortabilitySubset) {
        requiredExtensions.push_back(kPortabilitySubsetExt);
    }

    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physicalDevice_, &supported);

    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = supported.samplerAnisotropy;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
    createInfo.ppEnabledExtensionNames = requiredExtensions.data();
    createInfo.pEnabledFeatures = &features;

    const VkResult createResult = vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_);
    if (createResult != VK_SUCCESS) {
        std::fprintf(stderr, "[D72] vkCreateDevice failed (%d).\n", static_cast<int>(createResult));
        return false;
    }

    vkGetDeviceQueue(device_, queueFamilies_.graphicsFamily.value(), 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, queueFamilies_.presentFamily.value(), 0, &presentQueue_);

    std::fprintf(stdout, "[D72] Logical device created successfully.\n");
    return true;
}

void DeviceContext::shutdown()
{
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
        graphicsQueue_ = VK_NULL_HANDLE;
        presentQueue_ = VK_NULL_HANDLE;
    }
    physicalDevice_ = VK_NULL_HANDLE;
}

} // namespace dash::vkexp
