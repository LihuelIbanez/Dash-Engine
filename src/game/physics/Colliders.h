#pragma once

#include <cstdint>

namespace dash::physics {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Aabb {
    Vec3 min;
    Vec3 max;
};

enum class ColliderType : uint8_t {
    Box,
    Plane
};

struct BoxCollider {
    Vec3 halfExtents{0.5f, 0.5f, 0.5f};
};

struct PlaneCollider {
    Vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;
};

} // namespace dash::physics
