#include "game/physics/PhysicsWorld.h"

#include <algorithm>

namespace dash::physics {

static Vec3 operator+(const Vec3& a, const Vec3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

static Vec3 operator-(const Vec3& a, const Vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

static Vec3 operator*(const Vec3& a, float s)
{
    return {a.x * s, a.y * s, a.z * s};
}

bool PhysicsWorld::init(std::unique_ptr<IPhysicsBackend> backend)
{
    backend_ = std::move(backend);
    bodies_.clear();
    prevContacts_.clear();
    currContacts_.clear();
    return backend_ != nullptr;
}

int PhysicsWorld::createDynamicBox(const Vec3& position, const Vec3& halfExtents, float mass)
{
    Body body{};
    body.dynamic = true;
    body.colliderType = ColliderType::Box;
    body.position = position;
    body.halfExtents = halfExtents;
    body.mass = mass > 0.0f ? mass : 1.0f;
    bodies_.push_back(body);
    return static_cast<int>(bodies_.size()) - 1;
}

int PhysicsWorld::createStaticPlane(float y)
{
    Body body{};
    body.dynamic = false;
    body.colliderType = ColliderType::Plane;
    body.planeY = y;
    body.position = {0.0f, y, 0.0f};
    bodies_.push_back(body);
    return static_cast<int>(bodies_.size()) - 1;
}

bool PhysicsWorld::validBody(int bodyId) const
{
    return bodyId >= 0 && static_cast<size_t>(bodyId) < bodies_.size();
}

Aabb PhysicsWorld::worldAabb(int bodyId) const
{
    if (!validBody(bodyId)) return {};
    const Body& b = bodies_[static_cast<size_t>(bodyId)];
    if (b.colliderType != ColliderType::Box) {
        return {{-10000.0f, b.planeY - 0.001f, -10000.0f}, {10000.0f, b.planeY + 0.001f, 10000.0f}};
    }

    return {
        b.position - b.halfExtents,
        b.position + b.halfExtents
    };
}

bool PhysicsWorld::overlapAabb(const Aabb& a, const Aabb& b)
{
    return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
           (a.min.y <= b.max.y && a.max.y >= b.min.y) &&
           (a.min.z <= b.max.z && a.max.z >= b.min.z);
}

void PhysicsWorld::step(float dt)
{
    if (dt <= 0.0f) return;

    for (Body& body : bodies_) {
        if (!body.dynamic || body.colliderType != ColliderType::Box) continue;

        body.velocity = body.velocity + gravity_ * dt;
        body.position = body.position + body.velocity * dt;

        for (const Body& other : bodies_) {
            if (other.colliderType != ColliderType::Plane) continue;

            const float floorY = other.planeY;
            const float minY = body.position.y - body.halfExtents.y;
            if (minY < floorY) {
                body.position.y = floorY + body.halfExtents.y;
                if (body.velocity.y < 0.0f) {
                    body.velocity.y = -body.velocity.y * restitution_;
                    if (body.velocity.y < 0.03f) body.velocity.y = 0.0f;
                }
            }
        }
    }

    currContacts_.clear();
    for (int i = 0; i < static_cast<int>(bodies_.size()); ++i) {
        for (int j = i + 1; j < static_cast<int>(bodies_.size()); ++j) {
            const Aabb a = worldAabb(i);
            const Aabb b = worldAabb(j);
            if (overlapAabb(a, b)) {
                currContacts_.insert({i, j});
            }
        }
    }

    if (collisionCallback_) {
        for (const auto& p : currContacts_) {
            if (prevContacts_.find(p) == prevContacts_.end()) {
                collisionCallback_({CollisionEventType::Enter, p.first, p.second});
            } else {
                collisionCallback_({CollisionEventType::Stay, p.first, p.second});
            }
        }
        for (const auto& p : prevContacts_) {
            if (currContacts_.find(p) == currContacts_.end()) {
                collisionCallback_({CollisionEventType::Exit, p.first, p.second});
            }
        }
    }

    prevContacts_ = currContacts_;
}

void PhysicsWorld::applyImpulse(int bodyId, const Vec3& impulse)
{
    if (!validBody(bodyId)) return;
    Body& body = bodies_[static_cast<size_t>(bodyId)];
    if (!body.dynamic || body.mass <= 0.0f) return;

    const Vec3 dv = impulse * (1.0f / body.mass);
    body.velocity = body.velocity + dv;
}

Vec3 PhysicsWorld::position(int bodyId) const
{
    if (!validBody(bodyId)) return {};
    return bodies_[static_cast<size_t>(bodyId)].position;
}

Vec3 PhysicsWorld::velocity(int bodyId) const
{
    if (!validBody(bodyId)) return {};
    return bodies_[static_cast<size_t>(bodyId)].velocity;
}

} // namespace dash::physics
