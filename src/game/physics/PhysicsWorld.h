#pragma once

#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "game/physics/Colliders.h"
#include "game/physics/IPhysicsBackend.h"

namespace dash::physics {

enum class CollisionEventType {
    Enter,
    Stay,
    Exit
};

struct CollisionEvent {
    CollisionEventType type{};
    int a = -1;
    int b = -1;
};

class PhysicsWorld {
public:
    PhysicsWorld() = default;
    ~PhysicsWorld() = default;

    bool init(std::unique_ptr<IPhysicsBackend> backend = std::make_unique<BuiltinPhysicsBackend>());

    int createDynamicBox(const Vec3& position, const Vec3& halfExtents, float mass = 1.0f);
    int createStaticPlane(float y);

    void step(float dt);
    void applyImpulse(int bodyId, const Vec3& impulse);

    Vec3 position(int bodyId) const;
    Vec3 velocity(int bodyId) const;
    Aabb worldAabb(int bodyId) const;

    void setGravity(const Vec3& gravity) { gravity_ = gravity; }
    void setRestitution(float restitution) { restitution_ = restitution; }
    void setCollisionCallback(std::function<void(const CollisionEvent&)> callback) { collisionCallback_ = std::move(callback); }

private:
    struct Body {
        bool dynamic = false;
        ColliderType colliderType = ColliderType::Box;
        Vec3 position{};
        Vec3 velocity{};
        Vec3 halfExtents{0.5f, 0.5f, 0.5f};
        float mass = 1.0f;
        float planeY = 0.0f;
    };

    bool validBody(int bodyId) const;
    static bool overlapAabb(const Aabb& a, const Aabb& b);

    std::vector<Body> bodies_;
    std::unique_ptr<IPhysicsBackend> backend_;
    Vec3 gravity_{0.0f, -9.8f, 0.0f};
    float restitution_ = 0.25f;

    std::set<std::pair<int, int>> prevContacts_;
    std::set<std::pair<int, int>> currContacts_;
    std::function<void(const CollisionEvent&)> collisionCallback_;
};

} // namespace dash::physics
