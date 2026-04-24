#pragma once

#include "game/physics/PhysicsWorld.h"

namespace dash::vkexp {

struct RenderInstance {
    dash::physics::Vec3 position{};
    dash::physics::Vec3 scale{1.0f, 1.0f, 1.0f};
    dash::physics::Vec3 color{0.7f, 0.7f, 0.7f};
    bool isPlayer = false;
};

} // namespace dash::vkexp
