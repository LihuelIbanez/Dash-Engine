#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "rendering/mesh/MeshBuffers.h"
#include "rendering/mesh/ProceduralMesh.h"

// ─────────────────────────────────────────────────────────────────────────────
// Glue between the Vulkan-free generators and MeshBuffers. Header-only and
// shared on purpose: Renderer::resolveMesh (runtime) and
// EditorVkContext::resolveMesh (viewport) must resolve a "proc:" id the exact
// same way, or one of the two silently falls back to the builtin cube.
// ─────────────────────────────────────────────────────────────────────────────

namespace dash::procmesh {

// Generates `meshId` and uploads it into `out`. Returns false (and logs why)
// when the id is malformed or the upload fails, so the caller falls back.
inline bool uploadProceduralMesh(const std::string& meshId, const char* tag,
                                 VkPhysicalDevice physicalDevice, VkDevice device,
                                 dash::vkexp::MeshBuffers& out)
{
    static int s_generated = 0;

    ModelParams params;
    if (!parseMeshId(meshId, params)) {
        std::fprintf(stderr, "[ProcMesh] %s invalid procedural mesh id: '%s'\n", tag,
                     meshId.c_str());
        return false;
    }

    const MeshData data = generate(params);
    if (data.empty()) {
        std::fprintf(stderr, "[ProcMesh] %s generated an empty mesh for '%s'\n", tag,
                     meshId.c_str());
        return false;
    }

    const uint32_t vertexBytes =
        static_cast<uint32_t>(data.vertices.size() * sizeof(dash::vkexp::Vertex));
    const uint32_t indexBytes = static_cast<uint32_t>(data.indices.size() * sizeof(uint32_t));
    if (!out.initFromData(physicalDevice, device, data.vertices.data(), vertexBytes,
                          data.indices.data(), indexBytes,
                          static_cast<uint32_t>(data.indices.size()))) {
        std::fprintf(stderr, "[ProcMesh] %s upload failed for '%s'\n", tag, meshId.c_str());
        return false;
    }

    ++s_generated;
    std::printf("[ProcMesh] %s #%d '%s' kind=%s seed=%u part=%s -> %zu verts / %zu idx / %zu tris, "
                "bounds %.2f x %.2f x %.2f\n",
                tag, s_generated, meshId.c_str(), kindName(params.kind), params.seed,
                partName(params.part), data.vertices.size(), data.indices.size(),
                data.triangleCount(), static_cast<double>(data.extent(0)),
                static_cast<double>(data.extent(1)), static_cast<double>(data.extent(2)));
    std::fflush(stdout);
    return true;
}

} // namespace dash::procmesh
