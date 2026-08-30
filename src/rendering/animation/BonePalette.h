#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Bone palette — the CPU side of GPU bone storage.
//
// Storage choice: uniform buffer, not SSBO. One palette is
// kMaxBonesPerSkeleton * mat4 = 128 * 64 = 8192 bytes, half of the 16 KiB
// `maxUniformBufferRange` every Vulkan implementation is required to support,
// so no device query or fallback path is needed. UBOs are also the cheaper
// read-only path on MoltenVK and tile GPUs, and the array size can stay a
// compile-time constant in skinned.vert. An SSBO would only start paying off
// with variable-length skeletons or thousands of skinned instances per frame.
//
// Bone limit: 128 bones per skeleton (dash::vkexp::kMaxBonesPerSkeleton, and
// `const int kMaxBones` in assets/shaders/skinned.vert — the three must match).
// Skeleton::exceedsGpuLimit() flags anything above it at import time.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <cstring>

#include "rendering/mesh/SkinnedVertex.h"

namespace dash::anim {

inline constexpr uint32_t kBonePaletteMatrixCount = dash::vkexp::kMaxBonesPerSkeleton;
inline constexpr uint32_t kBonePaletteFloats = kBonePaletteMatrixCount * 16;
inline constexpr uint32_t kBonePaletteBytes =
    kBonePaletteFloats * static_cast<uint32_t>(sizeof(float));

// Fills `count` column-major mat4 slots with the identity.
inline void writeIdentityMatrices(float* dst, uint32_t count)
{
    if (dst == nullptr) return;
    std::memset(dst, 0, static_cast<size_t>(count) * 16 * sizeof(float));
    for (uint32_t i = 0; i < count; ++i) {
        float* m = dst + static_cast<size_t>(i) * 16;
        m[0] = 1.0f;
        m[5] = 1.0f;
        m[10] = 1.0f;
        m[15] = 1.0f;
    }
}

// Ring of fixed-size slots inside one host-visible, persistently mapped buffer.
// Every skinned draw claims a slot and addresses it with a dynamic UBO offset,
// so a whole frame's palettes live in a single buffer and a single descriptor.
//
// The buffer is partitioned into `regionCount` disjoint regions, one per frame
// in flight, because the memory is host-visible and written by the CPU while
// the GPU may still be reading the previous frame's poses. A single shared
// region would let frame N overwrite slots frame N-1 has not consumed yet — a
// data race the validation layers cannot see (every API call stays legal), so
// it surfaces only as flickering or corrupt poses under load. `beginFrame()`
// takes the frame index and moves the write cursor to that frame's region.
//
// One partitioned buffer rather than N separate buffers: the dynamic offset is
// already in the draw path, so the region base costs nothing, and the single
// mapping and single descriptor stay unchanged.
struct BonePalette {
    unsigned char* mapped = nullptr;
    uint32_t slotStride = 0;   // >= kBonePaletteBytes, padded to the device alignment
    uint32_t regionSlots = 0;  // slots one frame in flight may claim
    uint32_t regionCount = 1;  // frames in flight the buffer is partitioned for
    uint32_t region = 0;       // region the current frame writes into
    uint32_t nextSlot = 0;     // slots already claimed inside `region`

    uint32_t totalSlots() const { return regionSlots * regionCount; }
    uint32_t regionStride() const { return regionSlots * slotStride; }
    uint32_t regionBase() const { return region * regionStride(); }
    uint64_t bufferBytes() const
    {
        return static_cast<uint64_t>(totalSlots()) * slotStride;
    }

    // Rewinds the cursor into the region that belongs to `frameIndex`. Call once
    // per frame, before any writeSlot() of that frame.
    void beginFrame(uint32_t frameIndex)
    {
        region = regionCount > 0 ? (frameIndex % regionCount) : 0;
        nextSlot = 0;
    }

    bool usable() const
    {
        return mapped != nullptr && regionSlots > 0 && regionCount > 0
            && slotStride >= kBonePaletteBytes;
    }

    // Copies at most kBonePaletteMatrixCount matrices into the next free slot of
    // the current frame's region and returns its byte offset; -1 once that
    // region is exhausted.
    int64_t writeSlot(const float* matrices, uint32_t matrixCount)
    {
        if (!usable() || nextSlot >= regionSlots) return -1;

        const uint32_t clamped = matrixCount < kBonePaletteMatrixCount
                                     ? matrixCount
                                     : kBonePaletteMatrixCount;
        const uint32_t offset = regionBase() + nextSlot * slotStride;
        if (matrices != nullptr && clamped > 0) {
            std::memcpy(mapped + offset, matrices,
                        static_cast<size_t>(clamped) * 16 * sizeof(float));
        }

        ++nextSlot;
        return static_cast<int64_t>(offset);
    }
};

} // namespace dash::anim
