#pragma once

#include <string>
#include <vector>

#include "rendering/mesh/Vertex.h"

namespace dash::vkexp {

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::string diffuseTexturePath;  // relative to model file, empty if none
};

} // namespace dash::vkexp
