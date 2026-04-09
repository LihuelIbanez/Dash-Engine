#include "game/physics/DebugPhysicsDraw.h"

#include <cstdio>

#include "game/physics/PhysicsWorld.h"

namespace dash::physics {

void DebugPhysicsDraw::logBodyAabb(const PhysicsWorld& world, int bodyId, const char* label)
{
    const Aabb box = world.worldAabb(bodyId);
    std::printf(
        "[D82][DebugDraw] %s AABB min(%.3f, %.3f, %.3f) max(%.3f, %.3f, %.3f)\n",
        label ? label : "body",
        box.min.x, box.min.y, box.min.z,
        box.max.x, box.max.y, box.max.z);
}

} // namespace dash::physics
