#pragma once

#include <array>

namespace dash::vkexp {

struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    std::array<float, 2> texCoord;
};

} // namespace dash::vkexp
