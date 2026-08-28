#pragma once

#include <array>
#include <cstdint>

namespace dash::vkexp {

// Skinning attributes live in their own vertex stream (binding 1) instead of
// being interleaved into Vertex, so the static 32-byte layout stays untouched
// and the same mesh can feed both the static and the skinned pipeline.
struct SkinnedVertex {
    std::array<uint16_t, 4> boneIndices{{0, 0, 0, 0}};
    std::array<float, 4>    boneWeights{{0.0f, 0.0f, 0.0f, 0.0f}};
};

// Matches the bone matrix array declared in assets/shaders/skinned.vert.
inline constexpr uint32_t kMaxBonesPerSkeleton = 128;
inline constexpr uint32_t kMaxBoneInfluences = 4;

} // namespace dash::vkexp
