#pragma once

#include <array>
#include <cstdint>

namespace dash::vkexp {

// Terrain-specific vertex with per-vertex color and texture blend data
struct TerrainVkVertex {
    std::array<float, 3> position;   // (x, height, z) Y-up       12B
    std::array<float, 3> normal;     // face normal                12B
    std::array<float, 3> color;      // RGB terrain color [0..1]   12B
    uint32_t texIndicesPacked = 0;   // 4 × uint8 packed           4B
    uint32_t texWeightsPacked = 0;   // 4 × uint8 packed           4B
    uint16_t flags = 0;              // bit 0=cliffWall             2B
    uint16_t pad   = 0;              // alignment                   2B
    float ao = 1.0f;                 // baked occlusion, 1=open     4B
};                                   // Total: 52B

} // namespace dash::vkexp
