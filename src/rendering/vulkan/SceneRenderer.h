#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "rendering/mesh/MeshBuffers.h"
#include "rendering/vulkan/EditorBridge.h"
#include "rendering/vulkan/RenderTypes.h"
#include "rendering/vulkan/VkMath.h"

namespace dash::vkexp {

// ─────────────────────────────────────────────────────────────────────────────
// SceneRenderer — the single implementation of "draw the scene entities".
//
// Both the editor viewport and the standalone runtime go through here. They
// still own separate Vulkan devices and swapchains (different windows), but the
// draw logic — culling, mesh/material binding, billboards — lives once so a
// feature cannot land in one path and silently miss the other.
// ─────────────────────────────────────────────────────────────────────────────

// mat4 model (16 floats) + color/alpha (4) + lightDir/intensity (4).
inline constexpr std::size_t kInstancePushConstantFloats = 24;

void buildInstancePushConstants(const Mat4& model,
                                float r, float g, float b, float a,
                                const LightingParams& light,
                                float (&out)[kInstancePushConstantFloats]);

// Per-instance resources resolved by the caller, aligned by index with the
// instance vector. Each side fills it from its own mesh/material cache.
struct InstanceResources {
    const MeshBuffers* mesh = nullptr;                // nullptr → fallbackMesh
    VkDescriptorSet    materialSet = VK_NULL_HANDLE;  // null    → defaultSet
    float              tint[3] = {1.0f, 1.0f, 1.0f};
};

struct SceneDrawParams {
    VkPipeline         opaquePipeline    = VK_NULL_HANDLE;
    VkPipelineLayout   opaqueLayout      = VK_NULL_HANDLE;
    VkPipeline         billboardPipeline = VK_NULL_HANDLE;
    VkPipelineLayout   billboardLayout   = VK_NULL_HANDLE;
    VkDescriptorSet    defaultSet        = VK_NULL_HANDLE;
    const MeshBuffers* fallbackMesh      = nullptr;

    Mat4 viewProj{};
    Vec3 cameraRight{1.0f, 0.0f, 0.0f};
    Vec3 cameraUp{0.0f, 1.0f, 0.0f};
};

struct SceneDrawStats {
    uint32_t drawn = 0;
    uint32_t culled = 0;
};

// Records the opaque instance pass followed by the transparent billboard pass.
// The render pass must already be begun and the opaque pipeline bound.
SceneDrawStats drawSceneInstances(VkCommandBuffer cmd,
                                  const std::vector<RenderInstance>& instances,
                                  const std::vector<InstanceResources>& resources,
                                  const LightingParams& lighting,
                                  const SceneDrawParams& params);

} // namespace dash::vkexp
