#pragma once
#include "SceneData.h"

#include <cmath>
#include <cstdint>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// EntityHierarchy — parent/child transform composition over EntityData::parentId
//
// Header-only on purpose: the undoable commands under src/editor/commands need
// it and those are compiled into several test targets with different link sets.
// ─────────────────────────────────────────────────────────────────────────────
namespace dash::editor {

// Scene convention: x/y is the horizontal tile plane, z is height.
struct Transform3D {
    float x = 0.f, y = 0.f, z = 0.f;
    float yawDeg = 0.f, pitchDeg = 0.f, rollDeg = 0.f;
    float scale = 1.f;
};

// Guards against corrupted scenes where parentId links form a cycle.
inline constexpr int kMaxHierarchyDepth = 64;

inline const EntityData* findEntity(const SceneData& scene, uint64_t id)
{
    if (id == 0) return nullptr;
    for (const auto& e : scene.entities)
        if (e.id == id) return &e;
    return nullptr;
}

inline EntityData* findEntity(SceneData& scene, uint64_t id)
{
    if (id == 0) return nullptr;
    for (auto& e : scene.entities)
        if (e.id == id) return &e;
    return nullptr;
}

inline Transform3D localTransform(const EntityData& e)
{
    for (const auto& c : e.components) {
        if (const auto* tf = std::get_if<TransformComponent>(&c)) {
            Transform3D t;
            t.x = tf->x;        t.y = tf->y;          t.z = tf->z;
            t.yawDeg = tf->yawDeg; t.pitchDeg = tf->pitchDeg; t.rollDeg = tf->rollDeg;
            t.scale = tf->scale;
            return t;
        }
    }
    Transform3D t;
    t.x = e.x;
    t.y = e.y;
    return t;
}

inline void setLocalTransform(EntityData& e, const Transform3D& t)
{
    for (auto& c : e.components) {
        if (auto* tf = std::get_if<TransformComponent>(&c)) {
            tf->x = t.x;        tf->y = t.y;          tf->z = t.z;
            tf->yawDeg = t.yawDeg; tf->pitchDeg = t.pitchDeg; tf->rollDeg = t.rollDeg;
            tf->scale = t.scale;
            e.x = t.x;
            e.y = t.y;
            return;
        }
    }
    TransformComponent tf;
    tf.x = t.x;        tf.y = t.y;          tf.z = t.z;
    tf.yawDeg = t.yawDeg; tf.pitchDeg = t.pitchDeg; tf.rollDeg = t.rollDeg;
    tf.scale = t.scale;
    e.components.push_back(tf);
    e.x = t.x;
    e.y = t.y;
}

// Only the parent's yaw rotates the child offset; pitch/roll are inherited
// additively, which matches what an isometric camera can actually show.
inline Transform3D composeTransform(const Transform3D& parent, const Transform3D& local)
{
    const float rad = parent.yawDeg * 3.14159265358979f / 180.f;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    const float ps = parent.scale;

    Transform3D w;
    w.x = parent.x + ps * (local.x * c - local.y * s);
    w.y = parent.y + ps * (local.x * s + local.y * c);
    w.z = parent.z + ps * local.z;
    w.yawDeg   = parent.yawDeg   + local.yawDeg;
    w.pitchDeg = parent.pitchDeg + local.pitchDeg;
    w.rollDeg  = parent.rollDeg  + local.rollDeg;
    w.scale    = ps * local.scale;
    return w;
}

// Exact inverse of composeTransform: world → parent-relative.
inline Transform3D relativeTransform(const Transform3D& parent, const Transform3D& world)
{
    const float rad = parent.yawDeg * 3.14159265358979f / 180.f;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    const float ps = (std::fabs(parent.scale) > 1e-6f) ? parent.scale : 1.f;

    const float dx = world.x - parent.x;
    const float dy = world.y - parent.y;
    const float dz = world.z - parent.z;

    Transform3D l;
    l.x = ( dx * c + dy * s) / ps;
    l.y = (-dx * s + dy * c) / ps;
    l.z = dz / ps;
    l.yawDeg   = world.yawDeg   - parent.yawDeg;
    l.pitchDeg = world.pitchDeg - parent.pitchDeg;
    l.rollDeg  = world.rollDeg  - parent.rollDeg;
    l.scale    = world.scale / ps;
    return l;
}

/// Chain of ancestors from the entity up to its root, entity first.
inline std::vector<uint64_t> ancestorChain(const SceneData& scene, uint64_t id)
{
    std::vector<uint64_t> chain;
    uint64_t cur = id;
    for (int depth = 0; depth < kMaxHierarchyDepth && cur != 0; ++depth) {
        const EntityData* e = findEntity(scene, cur);
        if (!e) break;
        chain.push_back(cur);
        cur = e->parentId;
    }
    return chain;
}

inline Transform3D worldTransform(const SceneData& scene, uint64_t id)
{
    const std::vector<uint64_t> chain = ancestorChain(scene, id);
    Transform3D acc;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const EntityData* e = findEntity(scene, *it);
        if (!e) continue;
        acc = (it == chain.rbegin()) ? localTransform(*e)
                                     : composeTransform(acc, localTransform(*e));
    }
    return acc;
}

/// Write a world transform back onto an entity, converted to parent space.
inline void setWorldTransform(SceneData& scene, uint64_t id, const Transform3D& world)
{
    EntityData* e = findEntity(scene, id);
    if (!e) return;
    if (e->parentId == 0) {
        setLocalTransform(*e, world);
        return;
    }
    setLocalTransform(*e, relativeTransform(worldTransform(scene, e->parentId), world));
}

inline bool isDescendantOf(const SceneData& scene, uint64_t candidate, uint64_t ancestor)
{
    if (candidate == 0 || ancestor == 0) return false;
    uint64_t cur = candidate;
    for (int depth = 0; depth < kMaxHierarchyDepth && cur != 0; ++depth) {
        const EntityData* e = findEntity(scene, cur);
        if (!e) return false;
        if (e->parentId == ancestor) return true;
        cur = e->parentId;
    }
    return false;
}

/// Reject re-parenting onto self or onto one of the entity's own descendants.
inline bool canReparent(const SceneData& scene, uint64_t childId, uint64_t newParentId)
{
    if (childId == 0) return false;
    if (childId == newParentId) return false;
    if (!findEntity(scene, childId)) return false;
    if (newParentId == 0) return true;
    if (!findEntity(scene, newParentId)) return false;
    return !isDescendantOf(scene, newParentId, childId);
}

inline std::vector<uint64_t> childrenOf(const SceneData& scene, uint64_t parentId)
{
    std::vector<uint64_t> out;
    for (const auto& e : scene.entities)
        if (e.parentId == parentId && e.id != parentId) out.push_back(e.id);
    return out;
}

/// Entities without a parent, plus any whose parent link is dangling.
inline std::vector<uint64_t> rootEntities(const SceneData& scene)
{
    std::vector<uint64_t> out;
    for (const auto& e : scene.entities)
        if (e.parentId == 0 || !findEntity(scene, e.parentId)) out.push_back(e.id);
    return out;
}

/// Copy with every transform resolved to world space and parenting dropped.
/// Renderers that ignore parentId consume this instead of the raw scene.
inline SceneData flattenHierarchy(const SceneData& scene)
{
    SceneData flat = scene;
    for (auto& e : flat.entities) {
        setLocalTransform(e, worldTransform(scene, e.id));
        e.parentId = 0;
    }
    return flat;
}

} // namespace dash::editor
