#include "rendering/vulkan/VulkanDiagnostics.h"

#include <sstream>
#include <vector>

#include <vulkan/vulkan.h>

namespace dash::vkexp {

static std::string vkVersionToString(uint32_t version)
{
    std::ostringstream oss;
    oss << VK_VERSION_MAJOR(version) << "." << VK_VERSION_MINOR(version) << "." << VK_VERSION_PATCH(version);
    return oss.str();
}

std::string VulkanDiagnostics::buildReport()
{
    std::ostringstream report;

    uint32_t apiVersion = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&apiVersion);
    report << "[D70] Vulkan API version available: " << vkVersionToString(apiVersion) << "\n";

    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> exts(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, exts.data());
    report << "[D70] Instance extensions: " << extCount << "\n";

    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
    report << "[D70] Validation layers available: " << layerCount << "\n";

    bool hasValidation = false;
    for (const auto& layer : layers) {
        if (std::string(layer.layerName) == "VK_LAYER_KHRONOS_validation") {
            hasValidation = true;
            break;
        }
    }
    report << "[D70] VK_LAYER_KHRONOS_validation: " << (hasValidation ? "YES" : "NO") << "\n";

    return report.str();
}

} // namespace dash::vkexp
