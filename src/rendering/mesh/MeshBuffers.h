#pragma once

#include <cstdint>
#include <string>

#include <vulkan/vulkan.h>

namespace dash::vkexp {

class MeshBuffers {
public:
    MeshBuffers() = default;
    ~MeshBuffers() = default;

    MeshBuffers(const MeshBuffers&) = delete;
    MeshBuffers& operator=(const MeshBuffers&) = delete;
    MeshBuffers(MeshBuffers&& other) noexcept;
    MeshBuffers& operator=(MeshBuffers&& other) noexcept;

    bool initCube(VkPhysicalDevice physicalDevice, VkDevice device);
    bool initFromGLTF(VkPhysicalDevice physicalDevice, VkDevice device,
                      const std::string& gltfPath);
    bool initFromData(VkPhysicalDevice physicalDevice, VkDevice device,
                      const void* vertexData, uint32_t vertexDataSize,
                      const void* indexData, uint32_t indexDataSize,
                      uint32_t indexCount,
                      VkIndexType indexType = VK_INDEX_TYPE_UINT32);

    // Loads a .dashmesh (v1 or v2); the skinning stream becomes a second vertex
    // buffer bound at binding 1 by the skinned pipeline.
    bool initFromDashMesh(VkPhysicalDevice physicalDevice, VkDevice device,
                          const std::string& dashMeshPath);

    void shutdown(VkDevice device);

    VkBuffer vertexBuffer() const { return vertexBuffer_; }
    VkBuffer skinBuffer() const { return skinBuffer_; }
    VkBuffer indexBuffer() const { return indexBuffer_; }
    uint32_t indexCount() const { return indexCount_; }
    uint32_t boneCount() const { return boneCount_; }
    bool isSkinned() const { return skinBuffer_ != VK_NULL_HANDLE; }
    VkIndexType indexType() const { return indexType_; }

private:
    bool createBuffer(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& outBuffer,
        VkDeviceMemory& outMemory);

    uint32_t findMemoryType(
        VkPhysicalDevice physicalDevice,
        uint32_t typeFilter,
        VkMemoryPropertyFlags properties);

    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    VkBuffer skinBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory skinMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;
    uint32_t indexCount_ = 0;
    uint32_t boneCount_ = 0;
    VkIndexType indexType_ = VK_INDEX_TYPE_UINT32;
};

} // namespace dash::vkexp
