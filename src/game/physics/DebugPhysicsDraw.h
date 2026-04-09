#pragma once

#include "game/physics/Colliders.h"

namespace dash::physics {

class PhysicsWorld;

class DebugPhysicsDraw {
public:
    static void logBodyAabb(const PhysicsWorld& world, int bodyId, const char* label);
};

} // namespace dash::physics
