#pragma once

#include <string>
#include <vulkan/vulkan.h>

namespace dash::vkexp {

struct TextureResource {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
};

class TextureLoader {
public:
    static bool loadFromFile(VkPhysicalDevice physicalDevice,
                             VkDevice device,
                             VkQueue graphicsQueue,
                             VkCommandPool commandPool,
                             const std::string& path,
                             TextureResource& out);

    static bool createDefaultWhite(VkPhysicalDevice physicalDevice,
                                    VkDevice device,
                                    VkQueue graphicsQueue,
                                    VkCommandPool commandPool,
                                    TextureResource& out);

    // Tight RGBA8 pixels straight to a sampled texture. Public because the VFX
    // atlas is generated in memory and never touches disk.
    static bool createTextureFromPixels(VkPhysicalDevice physicalDevice,
                                         VkDevice device,
                                         VkQueue graphicsQueue,
                                         VkCommandPool commandPool,
                                         const unsigned char* pixels,
                                         uint32_t width,
                                         uint32_t height,
                                         TextureResource& out);

    static void destroy(VkDevice device, TextureResource& tex);

private:
    static uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                                   uint32_t typeFilter,
                                   VkMemoryPropertyFlags properties);

    static void transitionImageLayout(VkDevice device,
                                       VkQueue queue,
                                       VkCommandPool commandPool,
                                       VkImage image,
                                       VkImageLayout oldLayout,
                                       VkImageLayout newLayout);
};

} // namespace dash::vkexp
