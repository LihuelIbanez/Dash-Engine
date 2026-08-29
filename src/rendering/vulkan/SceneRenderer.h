#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "rendering/animation/BonePalette.h"
#include "rendering/mesh/MeshBuffers.h"
#include "rendering/vulkan/EditorBridge.h"
#include "rendering/vulkan/RenderTypes.h"
#include "rendering/vulkan/ShadowMath.h"
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
// + lightParams (4: count, ambient, 2 spare) + material (4: metallic,
// roughness, 2 spare) = 128 bytes, exactly what Vulkan guarantees for push
// constants. There is no room left for another vec4.
inline constexpr std::size_t kInstancePushConstantFloats = 32;

// eyePos(3) + time(1) + fogStart(1) + fogEnd(1) + lightDir(3) + intensity(1)
// + lightColor(3) + ambient(1) + 2 spare, followed by the per-layer terrain
// roughness table. Shared by the terrain and water pipelines so both can be fed
// from the same array.
inline constexpr std::size_t kTerrainPushConstantFloats = 28;

// mat4 model + mat4 lightViewProj = 128 bytes, exactly the push constant size
// Vulkan guarantees. Used by the depth-only shadow pass, which needs no
// descriptor set for static casters as a result.
inline constexpr std::size_t kShadowPushConstantFloats = 32;

// mat4 lightViewProj + center + half extents + the two quad axes: also exactly
// 128 bytes. The billboard depth pass has no per-instance model matrix, so the
// quad is rebuilt in the vertex shader from these axes.
inline constexpr std::size_t kShadowBillboardPushConstantFloats = 32;

void buildInstancePushConstants(const Mat4& model,
                                float r, float g, float b, float a,
                                const LightingParams& light,
                                float (&out)[kInstancePushConstantFloats],
                                int sceneLightCount = 0,
                                float metallic = 0.0f,
                                float roughness = 0.8f);

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
    // World -> light clip space, one per cascade, plus the tuning values; see
    // assets/shaders/shadow_sample.glsl. shadowParams all zero disables the lookup.
    float shadowMatrices[kShadowCascades][16]{};
    float shadowSplits[4]{};     // xyz = far camera distance covered by each cascade
    float shadowTexels[4]{};     // xyz = world size of one texel, per cascade
    float shadowDepthBias[4]{};  // xyz = depth bias in light clip units, per cascade
    float shadowParams[4]{};
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
    float              metallic = 0.0f;
    float              roughness = 0.8f;

    // Skinning: non-null selects the skinned pipeline for this instance.
    const float* boneMatrices = nullptr;   // boneCount * 16 floats, column-major
    uint32_t     boneCount = 0;
    uint32_t     boneOffset = 0;           // byte offset of this instance's palette slot
};

struct SceneDrawParams {
    VkPipeline         opaquePipeline    = VK_NULL_HANDLE;
    VkPipelineLayout   opaqueLayout      = VK_NULL_HANDLE;
    // In the depth pass these carry the alpha-cut depth variant instead.
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

    // Depth-only shadow pass: skips every material/descriptor bind on meshes and
    // pushes `model + viewProj` instead of the lit instance block. `viewProj` is
    // then the light matrix, so culling happens against the light frustum for
    // free, and cameraRight/cameraUp are the light-aligned billboard axes.
    bool depthOnly = false;
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
