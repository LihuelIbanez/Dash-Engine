#pragma once

#include <string>

#include "game/physics/PhysicsWorld.h"

namespace dash::vkexp {

// Mirrors RenderComponent::renderMode (see src/core/components/Components.h).
enum class InstanceRenderMode : int {
    Mesh3D = 0,
    BillboardSprite = 1,
};

struct RenderInstance {
    dash::physics::Vec3 position{};
    dash::physics::Vec3 scale{1.0f, 1.0f, 1.0f};
    dash::physics::Vec3 color{0.7f, 0.7f, 0.7f};
    bool isPlayer = false;

    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float rollDeg = 0.0f;

    std::string meshId = "cube";
    std::string materialId;
    int renderMode = static_cast<int>(InstanceRenderMode::Mesh3D);
    int layer = 0;
    bool visible = true;
};

} // namespace dash::vkexp
