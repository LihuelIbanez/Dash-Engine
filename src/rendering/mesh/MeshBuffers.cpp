#include "rendering/mesh/MeshBuffers.h"

#include <array>
#include <cstring>

#include "rendering/mesh/Vertex.h"

namespace dash::vkexp {

MeshBuffers::MeshBuffers(MeshBuffers&& other) noexcept
    : vertexBuffer_(other.vertexBuffer_)
    , vertexMemory_(other.vertexMemory_)
    , indexBuffer_(other.indexBuffer_)
    , indexMemory_(other.indexMemory_)
    , indexCount_(other.indexCount_)
{
    other.vertexBuffer_ = VK_NULL_HANDLE;
    other.vertexMemory_ = VK_NULL_HANDLE;
    other.indexBuffer_ = VK_NULL_HANDLE;
    other.indexMemory_ = VK_NULL_HANDLE;
    other.indexCount_ = 0;
}

MeshBuffers& MeshBuffers::operator=(MeshBuffers&& other) noexcept
{
    if (this != &other) {
        vertexBuffer_ = other.vertexBuffer_;
        vertexMemory_ = other.vertexMemory_;
        indexBuffer_ = other.indexBuffer_;
        indexMemory_ = other.indexMemory_;
        indexCount_ = other.indexCount_;
        other.vertexBuffer_ = VK_NULL_HANDLE;
        other.vertexMemory_ = VK_NULL_HANDLE;
        other.indexBuffer_ = VK_NULL_HANDLE;
        other.indexMemory_ = VK_NULL_HANDLE;
        other.indexCount_ = 0;
    }
    return *this;
}

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
    // 24 vertices: 4 per face with per-face normals and basic UVs
    const std::array<Vertex, 24> vertices = {{
        // Front face (z = +1), normal = (0, 0, 1)
        {{{-1.0f, -1.0f,  1.0f}}, {{ 0.0f,  0.0f,  1.0f}}, {{0.0f, 0.0f}}},
        {{{ 1.0f, -1.0f,  1.0f}}, {{ 0.0f,  0.0f,  1.0f}}, {{1.0f, 0.0f}}},
        {{{ 1.0f,  1.0f,  1.0f}}, {{ 0.0f,  0.0f,  1.0f}}, {{1.0f, 1.0f}}},
        {{{-1.0f,  1.0f,  1.0f}}, {{ 0.0f,  0.0f,  1.0f}}, {{0.0f, 1.0f}}},
        // Back face (z = -1), normal = (0, 0, -1)
        {{{ 1.0f, -1.0f, -1.0f}}, {{ 0.0f,  0.0f, -1.0f}}, {{0.0f, 0.0f}}},
        {{{-1.0f, -1.0f, -1.0f}}, {{ 0.0f,  0.0f, -1.0f}}, {{1.0f, 0.0f}}},
        {{{-1.0f,  1.0f, -1.0f}}, {{ 0.0f,  0.0f, -1.0f}}, {{1.0f, 1.0f}}},
        {{{ 1.0f,  1.0f, -1.0f}}, {{ 0.0f,  0.0f, -1.0f}}, {{0.0f, 1.0f}}},
        // Left face (x = -1), normal = (-1, 0, 0)
        {{{-1.0f, -1.0f, -1.0f}}, {{-1.0f,  0.0f,  0.0f}}, {{0.0f, 0.0f}}},
        {{{-1.0f, -1.0f,  1.0f}}, {{-1.0f,  0.0f,  0.0f}}, {{1.0f, 0.0f}}},
        {{{-1.0f,  1.0f,  1.0f}}, {{-1.0f,  0.0f,  0.0f}}, {{1.0f, 1.0f}}},
        {{{-1.0f,  1.0f, -1.0f}}, {{-1.0f,  0.0f,  0.0f}}, {{0.0f, 1.0f}}},
        // Right face (x = +1), normal = (1, 0, 0)
        {{{ 1.0f, -1.0f,  1.0f}}, {{ 1.0f,  0.0f,  0.0f}}, {{0.0f, 0.0f}}},
        {{{ 1.0f, -1.0f, -1.0f}}, {{ 1.0f,  0.0f,  0.0f}}, {{1.0f, 0.0f}}},
        {{{ 1.0f,  1.0f, -1.0f}}, {{ 1.0f,  0.0f,  0.0f}}, {{1.0f, 1.0f}}},
        {{{ 1.0f,  1.0f,  1.0f}}, {{ 1.0f,  0.0f,  0.0f}}, {{0.0f, 1.0f}}},
        // Top face (y = +1), normal = (0, 1, 0)
        {{{-1.0f,  1.0f,  1.0f}}, {{ 0.0f,  1.0f,  0.0f}}, {{0.0f, 0.0f}}},
        {{{ 1.0f,  1.0f,  1.0f}}, {{ 0.0f,  1.0f,  0.0f}}, {{1.0f, 0.0f}}},
        {{{ 1.0f,  1.0f, -1.0f}}, {{ 0.0f,  1.0f,  0.0f}}, {{1.0f, 1.0f}}},
        {{{-1.0f,  1.0f, -1.0f}}, {{ 0.0f,  1.0f,  0.0f}}, {{0.0f, 1.0f}}},
        // Bottom face (y = -1), normal = (0, -1, 0)
        {{{-1.0f, -1.0f, -1.0f}}, {{ 0.0f, -1.0f,  0.0f}}, {{0.0f, 0.0f}}},
        {{{ 1.0f, -1.0f, -1.0f}}, {{ 0.0f, -1.0f,  0.0f}}, {{1.0f, 0.0f}}},
        {{{ 1.0f, -1.0f,  1.0f}}, {{ 0.0f, -1.0f,  0.0f}}, {{1.0f, 1.0f}}},
        {{{-1.0f, -1.0f,  1.0f}}, {{ 0.0f, -1.0f,  0.0f}}, {{0.0f, 1.0f}}},
    }};

    const std::array<uint16_t, 36> indices = {{
        0,  1,  2,  2,  3,  0,   // front
        4,  5,  6,  6,  7,  4,   // back
        8,  9,  10, 10, 11, 8,   // left
        12, 13, 14, 14, 15, 12,  // right
        16, 17, 18, 18, 19, 16,  // top
        20, 21, 22, 22, 23, 20,  // bottom
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

bool MeshBuffers::initFromData(VkPhysicalDevice physicalDevice, VkDevice device,
                                const void* vertexData, uint32_t vertexDataSize,
                                const void* indexData, uint32_t indexDataSize,
                                uint32_t numIndices)
{
    if (!createBuffer(physicalDevice, device, vertexDataSize,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      vertexBuffer_, vertexMemory_)) {
        return false;
    }

    if (!createBuffer(physicalDevice, device, indexDataSize,
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      indexBuffer_, indexMemory_)) {
        shutdown(device);
        return false;
    }

    void* mapped = nullptr;
    vkMapMemory(device, vertexMemory_, 0, vertexDataSize, 0, &mapped);
    std::memcpy(mapped, vertexData, vertexDataSize);
    vkUnmapMemory(device, vertexMemory_);

    vkMapMemory(device, indexMemory_, 0, indexDataSize, 0, &mapped);
    std::memcpy(mapped, indexData, indexDataSize);
    vkUnmapMemory(device, indexMemory_);

    indexCount_ = numIndices;
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
