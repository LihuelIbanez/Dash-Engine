#pragma once

#include <array>

namespace dash::vkexp {

// Terrain-specific vertex with per-vertex color (no texture)
struct TerrainVkVertex {
    std::array<float, 3> position;   // (x, height, z) Y-up
    std::array<float, 3> normal;     // face normal
    std::array<float, 3> color;      // RGB terrain color [0..1]
};

} // namespace dash::vkexp
