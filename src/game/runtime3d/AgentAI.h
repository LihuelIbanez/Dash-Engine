#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// AgentAI — pure decision layer for the 3D runtime combat simulation.
//
// No Vulkan, no SDL, no World: every function here takes numbers and returns
// numbers, so the FSM and the damage maths can be tested headless.
// ─────────────────────────────────────────────────────────────────────────────

namespace dash::runtime3d {

enum class AgentState { Idle, Patrol, Chase, Attack, Flee, Dead };

// Distances are in tile units, the same frame PlayerController walks in.
struct AgentStats {
    int   maxHealth       = 40;
    int   attack          = 7;
    int   defense         = 2;
    float moveSpeed       = 2.4f;
    float detectionRadius = 9.0f;
    float attackRadius    = 1.3f;
    float attackCooldown  = 1.2f;
    int   expReward       = 12;
    // Health ratio that breaks morale; 0 disables retreating entirely.
    float fleeHealthFraction = 0.25f;
    // Cliff levels this archetype can climb up / drop down. See cliffStepAllowed.
    int   maxClimbLevels  = 0;
    int   maxDropLevels   = 0;
};

// Countdowns the FSM reads. Kept out of AgentStats so nextState() stays pure.
struct AgentTimers {
    float idleRemaining   = 0.0f;
    float patrolRemaining = 0.0f;
};

// Morale check. Retreating outranks every other decision, and it only ends by
// getting out of the threat radius, so a wounded agent cannot flip between Flee
// and Chase on consecutive frames.
inline bool shouldFlee(const AgentStats& stats, float healthFraction, float distToTarget)
{
    if (stats.fleeHealthFraction <= 0.0f) return false;
    if (healthFraction > stats.fleeHealthFraction) return false;
    return distToTarget <= stats.detectionRadius * 1.5f;
}

// Leaving a state needs more distance than entering it, so an agent sitting on
// the edge of a radius does not flip state every frame.
inline AgentState nextState(AgentState current, float distToTarget,
                            const AgentStats& stats, const AgentTimers& timers,
                            float healthFraction = 1.0f)
{
    if (current == AgentState::Dead) return AgentState::Dead;

    if (shouldFlee(stats, healthFraction, distToTarget)) return AgentState::Flee;
    if (current == AgentState::Flee) return AgentState::Idle;

    const bool detected = distToTarget < stats.detectionRadius;
    const bool lost     = distToTarget > stats.detectionRadius * 1.5f;

    switch (current) {
    case AgentState::Idle:
        if (detected) return AgentState::Chase;
        if (timers.idleRemaining <= 0.0f) return AgentState::Patrol;
        return AgentState::Idle;

    case AgentState::Patrol:
        if (detected) return AgentState::Chase;
        if (timers.patrolRemaining <= 0.0f) return AgentState::Idle;
        return AgentState::Patrol;

    case AgentState::Chase:
        if (distToTarget <= stats.attackRadius) return AgentState::Attack;
        if (lost) return AgentState::Idle;
        return AgentState::Chase;

    case AgentState::Attack:
        if (lost) return AgentState::Idle;
        if (distToTarget > stats.attackRadius * 1.3f) return AgentState::Chase;
        return AgentState::Attack;

    case AgentState::Flee:
    case AgentState::Dead:
        break;
    }
    return current;
}

// Same shape as Character::rollDamage() minus the crit roll, so the 3D runtime
// stays deterministic frame to frame.
inline int rollDamage(int attack, int defense)
{
    return std::max(1, attack - defense / 2);
}

inline int applyDamage(int health, int damage)
{
    return std::max(0, health - std::max(0, damage));
}

// Crowd separation, mirroring the constants of the 2D AISystem.
inline constexpr float kMinSeparation      = 0.85f;
inline constexpr float kSeparationStrength = 0.55f;

// Displacement applied to A because B is too close. Zero when far enough apart.
inline void separationPush(float ax, float az, float bx, float bz,
                           float& outX, float& outZ)
{
    outX = 0.0f;
    outZ = 0.0f;

    const float dx = ax - bx;
    const float dz = az - bz;
    const float d2 = dx * dx + dz * dz;
    if (d2 >= kMinSeparation * kMinSeparation) return;

    const float d = std::sqrt(d2);
    if (d < 1e-4f) {
        // Exactly stacked: pick an arbitrary axis so they still come apart.
        outX = kSeparationStrength;
        return;
    }
    const float push = (kMinSeparation - d) / kMinSeparation * kSeparationStrength;
    outX = dx / d * push;
    outZ = dz / d * push;
}

// Yaw in degrees for a heading on the XZ plane; 0 looks down +X.
inline float headingYawDeg(float dx, float dz, float fallbackYawDeg)
{
    if (std::fabs(dx) < 1e-5f && std::fabs(dz) < 1e-5f) return fallbackYawDeg;
    return std::atan2(dz, dx) * 57.29577951f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Surround slots — the pack spreads around the target instead of piling on it
// ─────────────────────────────────────────────────────────────────────────────

// Two concentric rings of standing spots. The inner one sits just inside melee
// reach; latecomers wait on the outer one until an inner slot frees up.
struct SurroundRings {
    float innerRadius = 1.10f;
    int   innerSlots  = 6;
    float outerRadius = 2.45f;
    int   outerSlots  = 10;

    int totalSlots() const { return std::max(0, innerSlots) + std::max(0, outerSlots); }
};

// Offset from the target to the given slot. False (and a zero offset) when the
// index is out of range, which is how "no slot" degrades to "walk at the target".
inline bool surroundSlotOffset(const SurroundRings& rings, int slot,
                               float& outX, float& outZ)
{
    outX = 0.0f;
    outZ = 0.0f;
    if (slot < 0 || slot >= rings.totalSlots()) return false;

    constexpr float kTwoPi = 6.28318531f;
    float radius = rings.innerRadius;
    float angle  = 0.0f;
    if (slot < rings.innerSlots) {
        const int n = std::max(1, rings.innerSlots);
        angle = kTwoPi * static_cast<float>(slot) / static_cast<float>(n);
    } else {
        const int n = std::max(1, rings.outerSlots);
        radius = rings.outerRadius;
        // Half a step of phase so the outer ring lands in the inner ring's gaps.
        angle = kTwoPi * (static_cast<float>(slot - rings.innerSlots) + 0.5f)
              / static_cast<float>(n);
    }
    outX = std::cos(angle) * radius;
    outZ = std::sin(angle) * radius;
    return true;
}

struct SurroundActor {
    float x = 0.0f;
    float z = 0.0f;
    // Slot held on the previous deal. Kept whenever it is still available, so a
    // re-deal never makes two agents swap spots and cross through each other.
    int   preferredSlot = -1;
};

// Greedy assignment: actors already close to the target claim first, and each
// takes the nearest free slot, so nobody crosses the ring to reach its spot.
// Entry i of the result is the slot of actor i, or -1 when the rings are full.
inline std::vector<int> assignSurroundSlots(const std::vector<SurroundActor>& actors,
                                            float targetX, float targetZ,
                                            const SurroundRings& rings)
{
    std::vector<int> result(actors.size(), -1);
    const int slotCount = rings.totalSlots();
    if (slotCount <= 0 || actors.empty()) return result;

    std::vector<float> slotX(static_cast<size_t>(slotCount), 0.0f);
    std::vector<float> slotZ(static_cast<size_t>(slotCount), 0.0f);
    std::vector<char>  taken(static_cast<size_t>(slotCount), 0);
    for (int s = 0; s < slotCount; ++s) {
        float ox = 0.0f, oz = 0.0f;
        surroundSlotOffset(rings, s, ox, oz);
        slotX[static_cast<size_t>(s)] = targetX + ox;
        slotZ[static_cast<size_t>(s)] = targetZ + oz;
    }

    std::vector<size_t> order(actors.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    const auto distToTarget = [&](size_t i) {
        const float dx = actors[i].x - targetX;
        const float dz = actors[i].z - targetZ;
        return dx * dx + dz * dz;
    };
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return distToTarget(a) < distToTarget(b); });

    for (size_t i : order) {
        // Ring priority: the melee ring has to fill before anyone parks on the
        // outer one, otherwise agents arriving from far away all stop short at
        // whichever outer slot happens to be nearest and nobody ever engages.
        int lo = 0;
        int hi = std::min(rings.innerSlots, slotCount);
        bool innerFree = false;
        for (int s = 0; s < hi; ++s) {
            if (!taken[static_cast<size_t>(s)]) { innerFree = true; break; }
        }
        if (!innerFree) { lo = hi; hi = slotCount; }

        const int keep = actors[i].preferredSlot;
        if (keep >= lo && keep < hi && !taken[static_cast<size_t>(keep)]) {
            taken[static_cast<size_t>(keep)] = 1;
            result[i] = keep;
            continue;
        }

        int   best   = -1;
        float bestD2 = 0.0f;
        for (int s = lo; s < hi; ++s) {
            if (taken[static_cast<size_t>(s)]) continue;
            const float dx = actors[i].x - slotX[static_cast<size_t>(s)];
            const float dz = actors[i].z - slotZ[static_cast<size_t>(s)];
            const float d2 = dx * dx + dz * dz;
            if (best < 0 || d2 < bestD2) { best = s; bestD2 = d2; }
        }
        if (best < 0) break;
        taken[static_cast<size_t>(best)] = 1;
        result[i] = best;
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Patrol
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr float kPatrolRadius = 3.0f;

// Waypoints on a circle around the spawn, stepped by the golden angle so two
// consecutive ones are never neighbours and the route never repeats a segment.
inline void patrolPoint(float spawnX, float spawnZ, float radius,
                        uint32_t sequence, float& outX, float& outZ)
{
    const float angle = static_cast<float>(sequence) * 2.39996323f;
    outX = spawnX + std::cos(angle) * radius;
    outZ = spawnZ + std::sin(angle) * radius;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cliffs
// ─────────────────────────────────────────────────────────────────────────────

// One cliff level is CLIFF_STEP (12 world units) tall across a one-unit tile, so
// by default it is a sheer wall: nothing climbs it and nothing survives the drop.
// The limits exist so ramp archetypes can be allowed later without a rewrite.
inline bool cliffStepAllowed(int fromLevel, int toLevel,
                             int maxClimb = 0, int maxDrop = 0)
{
    const int delta = toLevel - fromLevel;
    return delta <= maxClimb && -delta <= maxDrop;
}

} // namespace dash::runtime3d
