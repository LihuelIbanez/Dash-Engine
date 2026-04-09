#pragma once

#include "game/physics/Colliders.h"

namespace dash::physics {

struct Transform3 {
    Vec3 position{0.0f, 0.0f, 0.0f};
};

class PhysicsWorld;

class TransformProxy {
public:
    void syncFromPhysics(const PhysicsWorld& world, int bodyId, Transform3& transform) const;
};

} // namespace dash::physics
