#include "rendering/mesh/MeshBuffers.h"

#include <array>
#include <cstring>

#include "rendering/mesh/Vertex.h"

namespace dash::vkexp {

uint32_t MeshBuffers::findMemoryType(
    VkPhysicalDevice physicalDevice,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) &&
            ((memProperties.memoryTypes[i].propertyFlags & properties) == properties)) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool MeshBuffers::createBuffer(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& outBuffer,
    VkDeviceMemory& outMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements memRequirements{};
    vkGetBufferMemoryRequirements(device, outBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);
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

bool MeshBuffers::initCube(VkPhysicalDevice physicalDevice, VkDevice device)
{
    const std::array<Vertex, 8> vertices = {{
        {{-1.00f, -1.00f, -1.00f}},
        {{ 1.00f, -1.00f, -1.00f}},
        {{ 1.00f,  1.00f, -1.00f}},
        {{-1.00f,  1.00f, -1.00f}},
        {{-1.00f, -1.00f,  1.00f}},
        {{ 1.00f, -1.00f,  1.00f}},
        {{ 1.00f,  1.00f,  1.00f}},
        {{-1.00f,  1.00f,  1.00f}},
    }};

    const std::array<uint16_t, 36> indices = {{
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        0, 4, 7, 7, 3, 0,
        1, 5, 6, 6, 2, 1,
        3, 2, 6, 6, 7, 3,
        0, 1, 5, 5, 4, 0,
    }};

    const VkDeviceSize vertexBufferSize = sizeof(vertices);
    const VkDeviceSize indexBufferSize = sizeof(indices);

    if (!createBuffer(
            physicalDevice,
            device,
            vertexBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            vertexBuffer_,
            vertexMemory_)) {
        return false;
    }

    if (!createBuffer(
            physicalDevice,
            device,
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            indexBuffer_,
            indexMemory_)) {
        shutdown(device);
        return false;
    }

    void* mapped = nullptr;
    vkMapMemory(device, vertexMemory_, 0, vertexBufferSize, 0, &mapped);
    std::memcpy(mapped, vertices.data(), static_cast<size_t>(vertexBufferSize));
    vkUnmapMemory(device, vertexMemory_);

    vkMapMemory(device, indexMemory_, 0, indexBufferSize, 0, &mapped);
    std::memcpy(mapped, indices.data(), static_cast<size_t>(indexBufferSize));
    vkUnmapMemory(device, indexMemory_);

    indexCount_ = static_cast<uint32_t>(indices.size());
    return true;
}

void MeshBuffers::shutdown(VkDevice device)
{
    if (indexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, indexBuffer_, nullptr);
        indexBuffer_ = VK_NULL_HANDLE;
    }
    if (indexMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, indexMemory_, nullptr);
        indexMemory_ = VK_NULL_HANDLE;
    }
    if (vertexBuffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vertexBuffer_, nullptr);
        vertexBuffer_ = VK_NULL_HANDLE;
    }
    if (vertexMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertexMemory_, nullptr);
        vertexMemory_ = VK_NULL_HANDLE;
    }
    indexCount_ = 0;
}

} // namespace dash::vkexp
