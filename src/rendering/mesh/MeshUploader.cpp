#include "rendering/mesh/MeshUploader.h"

#include <cstdio>

namespace dash::vkexp {

bool MeshUploader::upload(VkPhysicalDevice physicalDevice,
                           VkDevice device,
                           VkQueue /*graphicsQueue*/,
                           VkCommandPool /*commandPool*/,
                           const MeshData& meshData,
                           MeshBuffers& outBuffers)
{
    if (meshData.vertices.empty() || meshData.indices.empty()) {
        std::fprintf(stderr, "[MeshUploader] Empty mesh data\n");
        return false;
    }

    uint32_t vertexSize = static_cast<uint32_t>(sizeof(Vertex) * meshData.vertices.size());
    uint32_t indexSize = static_cast<uint32_t>(sizeof(uint32_t) * meshData.indices.size());
    uint32_t indexCount = static_cast<uint32_t>(meshData.indices.size());

    if (!outBuffers.initFromData(physicalDevice, device,
                                  meshData.vertices.data(), vertexSize,
                                  meshData.indices.data(), indexSize,
                                  indexCount)) {
        std::fprintf(stderr, "[MeshUploader] Failed to upload mesh to GPU\n");
        return false;
    }

    std::fprintf(stdout, "[MeshUploader] Uploaded %zu vertices, %zu indices\n",
                 meshData.vertices.size(), meshData.indices.size());
    return true;
}

} // namespace dash::vkexp
