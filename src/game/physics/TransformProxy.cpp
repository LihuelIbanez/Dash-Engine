#include "game/physics/TransformProxy.h"

#include "game/physics/PhysicsWorld.h"

namespace dash::physics {

void TransformProxy::syncFromPhysics(const PhysicsWorld& world, int bodyId, Transform3& transform) const
{
    transform.position = world.position(bodyId);
}

} // namespace dash::physics
