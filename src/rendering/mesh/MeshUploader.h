#pragma once

#include <vulkan/vulkan.h>
#include "rendering/mesh/MeshData.h"
#include "rendering/mesh/MeshBuffers.h"

namespace dash::vkexp {

class MeshUploader {
public:
    static bool upload(VkPhysicalDevice physicalDevice,
                       VkDevice device,
                       VkQueue graphicsQueue,
                       VkCommandPool commandPool,
                       const MeshData& meshData,
                       MeshBuffers& outBuffers);

private:
    static uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                                   uint32_t typeFilter,
                                   VkMemoryPropertyFlags properties);

    static bool createBufferAndUpload(VkPhysicalDevice physicalDevice,
                                      VkDevice device,
                                      VkQueue queue,
                                      VkCommandPool commandPool,
                                      const void* data,
                                      VkDeviceSize size,
                                      VkBufferUsageFlags usage,
                                      VkBuffer& outBuffer,
                                      VkDeviceMemory& outMemory);
};

} // namespace dash::vkexp
