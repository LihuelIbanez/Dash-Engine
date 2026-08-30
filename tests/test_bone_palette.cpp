// ─────────────────────────────────────────────────────────────────────────────
// BonePalette — pure allocator logic, no Vulkan.
//
// What matters here is the invariant the GPU depends on: the region a frame
// writes into must never overlap the region another frame in flight is reading.
// A single shared cursor (the old behaviour) breaks that silently — the API
// calls stay legal, so validation layers report nothing.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "rendering/animation/BonePalette.h"

static int g_failures = 0;

#define ASSERT(cond, msg)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  FAIL: %s\n", (msg));                              \
            ++g_failures;                                                    \
        } else {                                                             \
            std::printf("  ok:   %s\n", (msg));                              \
        }                                                                    \
    } while (0)

using dash::anim::BonePalette;
using dash::anim::kBonePaletteBytes;
using dash::anim::kBonePaletteMatrixCount;

namespace {

// Mirrors what the renderers build: stride padded to a device alignment.
constexpr uint32_t kStride      = 8192;   // == kBonePaletteBytes for 128 bones
constexpr uint32_t kRegionSlots = 4;
constexpr uint32_t kRegions     = 3;

struct Fixture {
    std::vector<unsigned char> memory;
    BonePalette palette;

    Fixture()
    {
        palette.slotStride  = kStride;
        palette.regionSlots = kRegionSlots;
        palette.regionCount = kRegions;
        memory.assign(static_cast<size_t>(palette.bufferBytes()), 0);
        palette.mapped = memory.data();
    }
};

std::vector<float> poseFilledWith(float value)
{
    std::vector<float> pose(kBonePaletteMatrixCount * 16, value);
    return pose;
}

} // namespace

// ── Test: the buffer is sized for every region ───────────────────────────────
static void test_buffer_size()
{
    std::printf("test_buffer_size\n");
    Fixture f;
    ASSERT(f.palette.usable(), "palette with stride >= kBonePaletteBytes is usable");
    ASSERT(f.palette.totalSlots() == kRegionSlots * kRegions, "totalSlots = slots x regions");
    ASSERT(f.palette.bufferBytes() ==
               static_cast<uint64_t>(kRegionSlots) * kRegions * kStride,
           "bufferBytes covers every region");
}

// ── Test: writeSlot advances by exactly slotStride ───────────────────────────
static void test_slot_stride()
{
    std::printf("test_slot_stride\n");
    Fixture f;
    f.palette.beginFrame(0);

    const std::vector<float> pose = poseFilledWith(1.0f);
    int64_t previous = -1;
    for (uint32_t i = 0; i < kRegionSlots; ++i) {
        const int64_t offset = f.palette.writeSlot(pose.data(), kBonePaletteMatrixCount);
        ASSERT(offset >= 0, "slot inside the region is granted");
        if (previous >= 0) {
            ASSERT(offset - previous == static_cast<int64_t>(kStride),
                   "consecutive slots are exactly slotStride apart");
        }
        previous = offset;
    }

    ASSERT(f.palette.writeSlot(pose.data(), kBonePaletteMatrixCount) < 0,
           "a frame cannot claim more than regionSlots slots");
}

// ── Test: distinct frames never share a byte ─────────────────────────────────
static void test_regions_do_not_overlap()
{
    std::printf("test_regions_do_not_overlap\n");
    Fixture f;

    struct Range { uint64_t begin; uint64_t end; uint32_t frame; };
    std::vector<Range> ranges;

    const std::vector<float> pose = poseFilledWith(1.0f);
    // Six frames over three regions: also covers the wrap-around.
    for (uint32_t frame = 0; frame < kRegions * 2; ++frame) {
        f.palette.beginFrame(frame);
        for (uint32_t i = 0; i < kRegionSlots; ++i) {
            const int64_t offset = f.palette.writeSlot(pose.data(), kBonePaletteMatrixCount);
            ASSERT(offset >= 0, "slot granted");
            ranges.push_back({static_cast<uint64_t>(offset),
                              static_cast<uint64_t>(offset) + kBonePaletteBytes,
                              frame});
        }
    }

    bool disjoint = true;
    bool insideBuffer = true;
    for (size_t a = 0; a < ranges.size(); ++a) {
        if (ranges[a].end > f.palette.bufferBytes()) insideBuffer = false;
        for (size_t b = a + 1; b < ranges.size(); ++b) {
            // Frames three apart legitimately reuse a region; anything closer
            // than regionCount must not, and slots inside one frame never do.
            const uint32_t distance = ranges[b].frame - ranges[a].frame;
            if (distance >= kRegions) continue;
            if (ranges[a].begin < ranges[b].end && ranges[b].begin < ranges[a].end) {
                disjoint = false;
            }
        }
    }
    ASSERT(disjoint, "no two frames in flight share a palette byte");
    ASSERT(insideBuffer, "every slot stays inside the allocation");

    // And the region a frame owns is stable across the wrap.
    f.palette.beginFrame(0);
    const int64_t first = f.palette.writeSlot(pose.data(), kBonePaletteMatrixCount);
    f.palette.beginFrame(kRegions);
    const int64_t wrapped = f.palette.writeSlot(pose.data(), kBonePaletteMatrixCount);
    ASSERT(first == wrapped, "frame N and frame N+regionCount map to the same region");
}

// ── Test: the per-frame rewind does not touch the other regions ──────────────
static void test_reset_does_not_clobber_region_in_use()
{
    std::printf("test_reset_does_not_clobber_region_in_use\n");
    Fixture f;

    // Frame 0 leaves a recognisable pose behind; the GPU is still reading it.
    const std::vector<float> inFlight = poseFilledWith(7.0f);
    f.palette.beginFrame(0);
    const int64_t inFlightOffset = f.palette.writeSlot(inFlight.data(), kBonePaletteMatrixCount);
    ASSERT(inFlightOffset == 0, "frame 0 starts at the beginning of the buffer");

    // Frame 1 rewinds its own cursor and fills its whole region.
    const std::vector<float> overwrite = poseFilledWith(-3.0f);
    f.palette.beginFrame(1);
    for (uint32_t i = 0; i < kRegionSlots; ++i) {
        const int64_t offset = f.palette.writeSlot(overwrite.data(), kBonePaletteMatrixCount);
        ASSERT(offset >= static_cast<int64_t>(f.palette.regionStride()),
               "frame 1 writes past frame 0's region");
    }

    // Frame 0's bytes must be untouched.
    bool intact = std::memcmp(f.memory.data() + inFlightOffset, inFlight.data(),
                              kBonePaletteBytes) == 0;
    ASSERT(intact, "frame 0's poses survive a full frame 1");

    // Same check the other way round, across the wrap back onto region 0.
    f.palette.beginFrame(2);
    for (uint32_t i = 0; i < kRegionSlots; ++i) {
        f.palette.writeSlot(overwrite.data(), kBonePaletteMatrixCount);
    }
    intact = std::memcmp(f.memory.data() + inFlightOffset, inFlight.data(),
                         kBonePaletteBytes) == 0;
    ASSERT(intact, "frame 0's poses survive every other region being refilled");
}

// ── Test: a palette that was never sized hands out nothing ───────────────────
static void test_unusable_palette()
{
    std::printf("test_unusable_palette\n");
    BonePalette empty;
    ASSERT(!empty.usable(), "default-constructed palette is unusable");
    ASSERT(empty.writeSlot(nullptr, 0) < 0, "unusable palette refuses writes");

    Fixture f;
    f.palette.regionCount = 0;
    ASSERT(!f.palette.usable(), "zero regions is unusable");
}

int main()
{
    std::printf("=== BonePalette ===\n");
    test_buffer_size();
    test_slot_stride();
    test_regions_do_not_overlap();
    test_reset_does_not_clobber_region_in_use();
    test_unusable_palette();

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
