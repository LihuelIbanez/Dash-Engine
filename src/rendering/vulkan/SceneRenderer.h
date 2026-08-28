#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "rendering/animation/BonePalette.h"
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

// mat4 model (16 floats) + color/alpha (4) + lightDir/intensity (4)
// + lightParams (4: count, ambient, specular strength, shininess) = 112 bytes,
// inside the 128 bytes Vulkan guarantees for push constants.
inline constexpr std::size_t kInstancePushConstantFloats = 28;

void buildInstancePushConstants(const Mat4& model,
                                float r, float g, float b, float a,
                                const LightingParams& light,
                                float (&out)[kInstancePushConstantFloats],
                                int sceneLightCount = 0);

// std140 mirror of the SceneLightsUBO block in assets/shaders/scene_lights.glsl.
// Too large for push constants (kMaxSceneLights * 64 bytes), so it travels as a
// per-frame uniform buffer bound at set 0, binding 2.
struct SceneLightGpu {
    float posType[4]{};   // xyz = world position, w = type
    float dirRange[4]{};  // xyz = emission direction, w = range
    float colorInt[4]{};  // rgb = color, a = intensity
    float cone[4]{};      // x = inner cosine, y = outer cosine
};

struct SceneLightsUbo {
    float cameraPos[4]{};
    SceneLightGpu lights[kMaxSceneLights]{};
};

// Fills `out` with at most kMaxSceneLights entries; returns how many were written.
int packSceneLights(const std::vector<SceneLight>* lights,
                    const Vec3& cameraPos,
                    SceneLightsUbo& out);

// Per-instance resources resolved by the caller, aligned by index with the
// instance vector. Each side fills it from its own mesh/material cache.
struct InstanceResources {
    const MeshBuffers* mesh = nullptr;                // nullptr → fallbackMesh
    VkDescriptorSet    materialSet = VK_NULL_HANDLE;  // null    → defaultSet
    float              tint[3] = {1.0f, 1.0f, 1.0f};

    // Skinning: non-null selects the skinned pipeline for this instance.
    const float* boneMatrices = nullptr;   // boneCount * 16 floats, column-major
    uint32_t     boneCount = 0;
    uint32_t     boneOffset = 0;           // byte offset of this instance's palette slot
};

struct SceneDrawParams {
    VkPipeline         opaquePipeline    = VK_NULL_HANDLE;
    VkPipelineLayout   opaqueLayout      = VK_NULL_HANDLE;
    VkPipeline         billboardPipeline = VK_NULL_HANDLE;
    VkPipelineLayout   billboardLayout   = VK_NULL_HANDLE;
    VkPipeline         skinnedPipeline   = VK_NULL_HANDLE;
    VkPipelineLayout   skinnedLayout     = VK_NULL_HANDLE;
    VkDescriptorSet    defaultSet        = VK_NULL_HANDLE;
    const MeshBuffers* fallbackMesh      = nullptr;

    // Bone palette for this frame: set 1 of the skinned layout, addressed with
    // one dynamic offset per skinned draw. Null disables the skinned pass.
    VkDescriptorSet     boneSet     = VK_NULL_HANDLE;
    dash::anim::BonePalette* bonePalette = nullptr;

    // Scene lights, already in render space. Empty falls back to the single
    // directional light carried in LightingParams.
    const std::vector<SceneLight>* lights = nullptr;

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
