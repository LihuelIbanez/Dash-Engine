#pragma once

#include <cstdint>
#include <string>

#include "core/components/Components.h"
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

    // Skeletal playback requested by the entity; `animation` is meaningless
    // unless hasAnimation is set.
    bool hasAnimation = false;
    AnimationComponent animation{};

    uint64_t entityId = 0;
};

// Upper bound baked into the shaders' light UBO.
inline constexpr int kMaxSceneLights = 8;

// One scene light already converted to render space. LightComponent lives in
// scene space (x/y tile plane, z height); this is what the shaders consume.
struct SceneLight {
    int   type = 0;            // 0 = directional, 1 = point, 2 = spot
    float posX = 0.f, posY = 0.f, posZ = 0.f;
    float dirX = 0.f, dirY = -1.f, dirZ = 0.f;
    float colorR = 1.f, colorG = 1.f, colorB = 1.f;
    float intensity = 1.f;
    float range = 10.f;
    float innerCos = 0.94f;
    float outerCos = 0.82f;
    // Mirrors LightComponent::castsShadows. Only the first directional light
    // with this set actually gets a shadow map.
    bool  castsShadows = false;
};

// A physics body requested by an entity's PhysicsComponent.
struct PhysicsSpawn {
    uint64_t entityId = 0;
    dash::physics::Vec3 position{};
    dash::physics::Vec3 halfExtents{0.3f, 0.3f, 0.3f};
    float mass = 1.0f;
    bool isStatic = false;
};

} // namespace dash::vkexp
