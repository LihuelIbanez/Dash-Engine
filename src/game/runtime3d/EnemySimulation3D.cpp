#include "game/runtime3d/EnemySimulation3D.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <variant>

#include "events/EventDispatcher.h"
#include "events/GameEvents.h"
#include "game/runtime3d/CliffNav.h"
#include "scene/SceneData.h"
#include "world/TerrainMesh.h"
#include "world/World.h"

namespace dash::runtime3d {

namespace {

constexpr float kPathRefreshSeconds = 0.5f;
constexpr float kWaypointReached    = 0.35f;
constexpr float kSlotReached        = 0.30f;
constexpr float kPatrolReached      = 0.45f;
constexpr float kPatrolSeconds      = 4.0f;
constexpr float kPatrolSpeedScale   = 0.45f;
// Repositioning inside melee range is a shuffle, not a charge.
constexpr float kSlotCreepScale     = 0.55f;
constexpr float kFleeSpeedScale     = 1.15f;
// Any per-frame vertical delta above this would mean the cliff guard leaked.
constexpr float kCliffJumpThreshold = 1.0f;

// scenes/default.json ships its enemies without components, so the archetype is
// picked from the entity name and everything else falls back to these numbers.
AgentStats statsForName(const std::string& name)
{
    AgentStats s;
    if (name == "Skeleton") {
        s.maxHealth = 34; s.attack = 8; s.defense = 2;
        s.moveSpeed = 2.6f; s.attackCooldown = 1.1f; s.expReward = 14;
        s.fleeHealthFraction = 0.0f;   // skeletons have no morale to break
    } else if (name == "Zombie") {
        s.maxHealth = 52; s.attack = 6; s.defense = 3;
        s.moveSpeed = 1.7f; s.attackCooldown = 1.5f; s.expReward = 11;
        s.fleeHealthFraction = 0.0f;
    } else if (name == "Fallen") {
        s.maxHealth = 26; s.attack = 5; s.defense = 1;
        s.moveSpeed = 3.1f; s.attackCooldown = 0.9f; s.expReward = 9;
        s.fleeHealthFraction = 0.35f;  // the Diablo classic: routs when hurt
    }
    return s;
}

float clampToWorld(float v)
{
    return std::clamp(v, 0.0f, static_cast<float>(WORLD_W) - 1.001f);
}

const char* stateName(AgentState s)
{
    switch (s) {
    case AgentState::Idle:   return "Idle";
    case AgentState::Patrol: return "Patrol";
    case AgentState::Chase:  return "Chase";
    case AgentState::Attack: return "Attack";
    case AgentState::Flee:   return "Flee";
    case AgentState::Dead:   return "Dead";
    }
    return "?";
}

} // namespace

EnemySimulation3D::EnemySimulation3D() = default;
EnemySimulation3D::~EnemySimulation3D() = default;

void EnemySimulation3D::build(const SceneData& scene,
                              const std::vector<RenderInstance>& instances)
{
    agents_.clear();
    groundResolved_ = false;

    for (const auto& e : scene.entities) {
        if (e.type == EntityData::Type::Player) {
            playerEntityId_ = e.id;
            continue;
        }
        if (e.type != EntityData::Type::Enemy) continue;

        // Scenes park lights and other helpers on Enemy entities; those are not
        // combatants.
        bool isHelper = false;
        for (const auto& c : e.components) {
            if (std::get_if<LightComponent>(&c)) { isHelper = true; break; }
            if (const auto* rc = std::get_if<RenderComponent>(&c)) {
                if (!rc->visible) { isHelper = true; break; }
            }
        }
        if (isHelper) continue;

        EnemyAgent agent;
        agent.entityId = e.id;
        agent.name = e.name;
        agent.stats = statsForName(e.name);
        agent.health = agent.stats.maxHealth;
        agent.x = e.x;
        agent.z = e.y;
        agent.y = 0.0f;
        agent.state = AgentState::Idle;
        agent.timers.idleRemaining = 0.25f;

        for (const auto& inst : instances) {
            if (inst.entityId != e.id) continue;
            agent.x = inst.position.x;
            agent.z = inst.position.z;
            agent.y = inst.position.y;
            agent.yawDeg = inst.yawDeg;
            break;
        }

        agent.spawnX = agent.x;
        agent.spawnZ = agent.z;
        agent.anim.setMachine(sharedEnemyStateMachine());

        agents_.push_back(std::move(agent));
    }

    playerHealth_ = playerMaxHealth_;
    playerAttackCooldown_ = 0.0f;
    slotTimer_ = 0.0f;
    cliffBlockedSteps_ = 0;
    cliffJumpEvents_ = 0;
    maxHeightJump_ = 0.0f;

    if (agents_.empty()) {
        navWorld_.reset();
        navWorldReady_ = false;
        return;
    }

    // Same seed as the rendered terrain, so A* walkability matches the ground.
    navWorld_ = std::make_unique<World>();
    navWorld_->generate(scene.worldSeed);
    for (const auto& ov : scene.tileOverrides) {
        if (ov.x < 0 || ov.x >= WORLD_W || ov.y < 0 || ov.y >= WORLD_H) continue;
        navWorld_->grid[static_cast<size_t>(ov.y)][static_cast<size_t>(ov.x)].walkable = ov.walkable;
    }
    navWorldReady_ = true;

    std::printf("[Sim3D] %zu enemy agent(s) armed (seed %u, player entity %llu).\n",
                agents_.size(), scene.worldSeed,
                static_cast<unsigned long long>(playerEntityId_));
}

void EnemySimulation3D::refreshPath(EnemyAgent& agent, float targetX, float targetZ,
                                    const TerrainMesh* terrain)
{
    agent.pathAge = 0.0f;
    agent.pathIndex = 0;
    if (!navWorldReady_) { agent.path.clear(); return; }

    const NavPoint start = GridNav::worldToTile(agent.x, agent.z);
    const NavPoint goal  = GridNav::worldToTile(targetX, targetZ);

    // The rendered terrain is the source of truth for cliffs, so nav and
    // geometry cannot drift apart; without it the filter is simply absent.
    GridNav::StepFilter filter;
    if (terrain) {
        filter = cliffStepFilter(*terrain, agent.stats.maxClimbLevels,
                                 agent.stats.maxDropLevels);
    }
    agent.path = GridNav::findPath(start.x, start.y, goal.x, goal.y, *navWorld_,
                                   2048, filter);
    if (agent.path.size() > 1) agent.pathIndex = 1;
}

void EnemySimulation3D::assignSlots(float playerX, float playerZ)
{
    std::vector<SurroundActor> actors;
    std::vector<size_t>        owners;
    actors.reserve(agents_.size());
    owners.reserve(agents_.size());

    for (size_t i = 0; i < agents_.size(); ++i) {
        EnemyAgent& a = agents_[i];
        const bool engaged = a.alive && (a.state == AgentState::Chase ||
                                         a.state == AgentState::Attack);
        if (!engaged) { a.slot = -1; continue; }
        actors.push_back({a.x, a.z, a.slot});
        owners.push_back(i);
    }
    if (actors.empty()) return;

    const std::vector<int> slots = assignSurroundSlots(actors, playerX, playerZ, rings_);
    for (size_t i = 0; i < owners.size(); ++i) {
        agents_[owners[i]].slot = slots[i];
    }
}

void EnemySimulation3D::moveAgent(EnemyAgent& agent, float stepX, float stepZ,
                                  const TerrainMesh* terrain)
{
    const float startY = agent.y;
    float nx = clampToWorld(agent.x + stepX);
    float nz = clampToWorld(agent.z + stepZ);

    if (terrain) {
        const NavPoint from = GridNav::worldToTile(agent.x, agent.z);
        const NavPoint to   = GridNav::worldToTile(nx, nz);
        const int climb = agent.stats.maxClimbLevels;
        const int drop  = agent.stats.maxDropLevels;
        if ((to.x != from.x || to.y != from.y) &&
            !cliffStepPassable(*terrain, from.x, from.y, to.x, to.y, climb, drop))
        {
            ++cliffBlockedSteps_;
            // Slide along the wall: keep whichever axis stays on this tier.
            const NavPoint alongX = GridNav::worldToTile(nx, agent.z);
            const NavPoint alongZ = GridNav::worldToTile(agent.x, nz);
            if (cliffStepPassable(*terrain, from.x, from.y, alongX.x, alongX.y, climb, drop)) {
                nz = agent.z;
            } else if (cliffStepPassable(*terrain, from.x, from.y, alongZ.x, alongZ.y, climb, drop)) {
                nx = agent.x;
            } else {
                nx = agent.x;
                nz = agent.z;
            }
            agent.path.clear();   // the plan walked into a wall: replan next tick
        }
    }

    agent.x = nx;
    agent.z = nz;

    const float g = terrain ? terrain->sampleHeight(agent.x, agent.z) : 0.0f;
    agent.y = g + agent.groundOffset;

    const float jump = std::fabs(agent.y - startY);
    maxHeightJump_ = std::max(maxHeightJump_, jump);
    if (jump > kCliffJumpThreshold) ++cliffJumpEvents_;
}

void EnemySimulation3D::update(float dt, float playerX, float playerZ,
                               const TerrainMesh* terrainMesh, bool terrainMeshReady,
                               EventDispatcher& events)
{
    if (agents_.empty() || dt <= 0.0f) return;

    const TerrainMesh* ground = terrainMeshReady ? terrainMesh : nullptr;
    lastPlayerX_ = playerX;
    lastPlayerZ_ = playerZ;

    if (!groundResolved_) {
        for (EnemyAgent& a : agents_) {
            const float g = ground ? ground->sampleHeight(a.x, a.z) : 0.0f;
            a.groundOffset = a.y - g;
        }
        groundResolved_ = true;
    }

    // Slots are re-dealt on the replanning beat, so nobody walks a stale path to
    // a spot that already belongs to someone else.
    slotTimer_ -= dt;
    if (slotTimer_ <= 0.0f) {
        assignSlots(playerX, playerZ);
        slotTimer_ = kPathRefreshSeconds;
    }

    const auto steerAlongPath = [&](EnemyAgent& agent, float goalX, float goalZ,
                                    float& dirX, float& dirZ) {
        agent.pathAge += dt;
        if (agent.pathAge >= kPathRefreshSeconds || agent.path.empty()) {
            refreshPath(agent, goalX, goalZ, ground);
        }

        while (agent.pathIndex < static_cast<int>(agent.path.size())) {
            float wx = 0.0f, wz = 0.0f;
            GridNav::tileToCentre(agent.path[static_cast<size_t>(agent.pathIndex)].x,
                                  agent.path[static_cast<size_t>(agent.pathIndex)].y, wx, wz);
            const float dx = wx - agent.x;
            const float dz = wz - agent.z;
            const float wdist = std::sqrt(dx * dx + dz * dz);
            if (wdist < kWaypointReached) { ++agent.pathIndex; continue; }
            dirX = dx / wdist;
            dirZ = dz / wdist;
            return;
        }

        // No usable path (water, cliff, out of budget): head straight at the
        // goal and let moveAgent() refuse anything that crosses a cliff.
        const float dx = goalX - agent.x;
        const float dz = goalZ - agent.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d > 1e-3f) { dirX = dx / d; dirZ = dz / d; }
    };

    // ── Per-agent AI and steering ────────────────────────────────────────────
    for (EnemyAgent& agent : agents_) {
        if (!agent.alive) continue;

        agent.attackCooldown = std::max(0.0f, agent.attackCooldown - dt);
        agent.timers.idleRemaining = std::max(0.0f, agent.timers.idleRemaining - dt);
        agent.timers.patrolRemaining = std::max(0.0f, agent.timers.patrolRemaining - dt);

        const float toPlayerX = playerX - agent.x;
        const float toPlayerZ = playerZ - agent.z;
        const float dist = std::sqrt(toPlayerX * toPlayerX + toPlayerZ * toPlayerZ);
        const float healthFraction = agent.stats.maxHealth > 0
            ? static_cast<float>(agent.health) / static_cast<float>(agent.stats.maxHealth)
            : 0.0f;

        const AgentState prev = agent.state;
        agent.state = nextState(prev, dist, agent.stats, agent.timers, healthFraction);
        if (agent.state != prev) {
            agent.animStateTime = 0.0f;
            switch (agent.state) {
            case AgentState::Idle:
                agent.timers.idleRemaining = 1.0f;
                agent.path.clear();
                agent.slot = -1;
                break;
            case AgentState::Patrol:
                agent.timers.patrolRemaining = kPatrolSeconds;
                agent.path.clear();
                agent.pathAge = kPathRefreshSeconds;
                patrolPoint(agent.spawnX, agent.spawnZ, kPatrolRadius, agent.patrolSeq++,
                            agent.patrolTargetX, agent.patrolTargetZ);
                agent.patrolTargetX = clampToWorld(agent.patrolTargetX);
                agent.patrolTargetZ = clampToWorld(agent.patrolTargetZ);
                break;
            case AgentState::Chase:
                agent.path.clear();
                agent.pathAge = kPathRefreshSeconds;   // force an immediate replan
                slotTimer_ = 0.0f;                     // and a fresh ring deal
                break;
            case AgentState::Attack:
                agent.path.clear();
                break;
            case AgentState::Flee:
                agent.path.clear();
                agent.slot = -1;
                break;
            case AgentState::Dead:
                break;
            }
        }

        float dirX = 0.0f, dirZ = 0.0f;
        float speedScale = 1.0f;

        switch (agent.state) {
        case AgentState::Patrol: {
            const float dx = agent.patrolTargetX - agent.x;
            const float dz = agent.patrolTargetZ - agent.z;
            if (dx * dx + dz * dz < kPatrolReached * kPatrolReached) {
                patrolPoint(agent.spawnX, agent.spawnZ, kPatrolRadius, agent.patrolSeq++,
                            agent.patrolTargetX, agent.patrolTargetZ);
                agent.patrolTargetX = clampToWorld(agent.patrolTargetX);
                agent.patrolTargetZ = clampToWorld(agent.patrolTargetZ);
                agent.path.clear();
            }
            steerAlongPath(agent, agent.patrolTargetX, agent.patrolTargetZ, dirX, dirZ);
            speedScale = kPatrolSpeedScale;
            break;
        }
        case AgentState::Chase: {
            float goalX = playerX, goalZ = playerZ;
            float ox = 0.0f, oz = 0.0f;
            if (surroundSlotOffset(rings_, agent.slot, ox, oz)) {
                goalX = clampToWorld(playerX + ox);
                goalZ = clampToWorld(playerZ + oz);
            }
            steerAlongPath(agent, goalX, goalZ, dirX, dirZ);
            break;
        }
        case AgentState::Attack: {
            // Already in reach: only shuffle if this one is standing in someone
            // else's spot, which is what keeps the ring evenly filled.
            float ox = 0.0f, oz = 0.0f;
            if (surroundSlotOffset(rings_, agent.slot, ox, oz)) {
                const float dx = (playerX + ox) - agent.x;
                const float dz = (playerZ + oz) - agent.z;
                const float d = std::sqrt(dx * dx + dz * dz);
                if (d > kSlotReached) {
                    dirX = dx / d;
                    dirZ = dz / d;
                    speedScale = kSlotCreepScale;
                }
            }
            break;
        }
        case AgentState::Flee: {
            if (dist > 1e-3f) {
                dirX = -toPlayerX / dist;
                dirZ = -toPlayerZ / dist;
                speedScale = kFleeSpeedScale;
            }
            break;
        }
        case AgentState::Idle:
        case AgentState::Dead:
            break;
        }

        float sepX = 0.0f, sepZ = 0.0f;
        for (const EnemyAgent& other : agents_) {
            if (!other.alive || other.entityId == agent.entityId) continue;
            float px = 0.0f, pz = 0.0f;
            separationPush(agent.x, agent.z, other.x, other.z, px, pz);
            sepX += px;
            sepZ += pz;
        }

        const float stepX = (dirX * agent.stats.moveSpeed * speedScale + sepX) * dt;
        const float stepZ = (dirZ * agent.stats.moveSpeed * speedScale + sepZ) * dt;
        moveAgent(agent, stepX, stepZ, ground);
        agent.yawDeg = headingYawDeg(stepX, stepZ, agent.yawDeg);
        agent.planarSpeed = dt > 0.0f
            ? std::sqrt(stepX * stepX + stepZ * stepZ) / dt
            : 0.0f;
    }

    resolveCombat(dt, playerX, playerZ, events);

    // ── Animation parameters, after combat so a swing lands the same frame ───
    for (EnemyAgent& agent : agents_) {
        AgentAnimSignals signals;
        signals.speed = agent.alive ? agent.planarSpeed : 0.0f;
        signals.attackStarted = agent.swungThisFrame;
        signals.died = agent.diedThisFrame;
        applyAgentAnimation(signals, agent.anim.parameters());
        agent.swungThisFrame = false;
        agent.diedThisFrame = false;

        agent.animStateTime += dt;
        if (agent.anim.step(agent.animStateTime / kAnimNominalClipSeconds).changed()) {
            agent.animStateTime = 0.0f;
        }
    }
}

void EnemySimulation3D::resolveCombat(float dt, float playerX, float playerZ,
                                      EventDispatcher& events)
{
    playerAttackCooldown_ = std::max(0.0f, playerAttackCooldown_ - dt);

    // ── Enemies hitting the player ───────────────────────────────────────────
    for (EnemyAgent& agent : agents_) {
        if (!agent.alive || agent.state != AgentState::Attack) continue;
        if (agent.attackCooldown > 0.0f || playerHealth_ <= 0) continue;

        agent.attackCooldown = agent.stats.attackCooldown;
        agent.swungThisFrame = true;

        const int dmg = rollDamage(agent.stats.attack, /*playerDefense=*/4);
        const int before = playerHealth_;
        playerHealth_ = applyDamage(before, dmg);

        DamageEvent de;
        de.attackerId = agent.entityId;
        de.targetId = playerEntityId_;
        de.targetName = "Player";
        de.damage = dmg;
        de.finalHealth = playerHealth_;
        events.emit(de);

        HealthChangeEvent hc;
        hc.entityId = playerEntityId_;
        hc.oldHealth = before;
        hc.newHealth = playerHealth_;
        hc.maxHealth = playerMaxHealth_;
        events.emit(hc);

        if (playerHealth_ == 0) {
            DeathEvent dead;
            dead.entityId = playerEntityId_;
            dead.x = playerX;
            dead.y = playerZ;
            dead.entityName = "Player";
            events.emit(dead);
        }
    }

    // ── Player striking back at the closest enemy in reach ───────────────────
    if (playerHealth_ <= 0 || playerAttackCooldown_ > 0.0f) return;

    EnemyAgent* victim = nullptr;
    float bestDist = playerAttackRadius_;
    for (EnemyAgent& agent : agents_) {
        if (!agent.alive) continue;
        const float dx = agent.x - playerX;
        const float dz = agent.z - playerZ;
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d <= bestDist) { bestDist = d; victim = &agent; }
    }
    if (!victim) return;

    playerAttackCooldown_ = playerAttackCooldownMax_;

    const int dmg = rollDamage(playerAttack_, victim->stats.defense);
    const int before = victim->health;
    victim->health = applyDamage(before, dmg);

    DamageEvent de;
    de.attackerId = playerEntityId_;
    de.targetId = victim->entityId;
    de.targetName = victim->name;
    de.damage = dmg;
    de.finalHealth = victim->health;
    events.emit(de);

    HealthChangeEvent hc;
    hc.entityId = victim->entityId;
    hc.oldHealth = before;
    hc.newHealth = victim->health;
    hc.maxHealth = victim->stats.maxHealth;
    events.emit(hc);

    if (victim->health == 0) {
        victim->alive = false;
        victim->state = AgentState::Dead;
        victim->path.clear();
        victim->slot = -1;
        victim->planarSpeed = 0.0f;
        victim->diedThisFrame = true;

        DeathEvent dead;
        dead.entityId = victim->entityId;
        dead.x = victim->x;
        dead.y = victim->z;
        dead.entityName = victim->name;
        dead.expReward = victim->stats.expReward;
        events.emit(dead);
    }
}

void EnemySimulation3D::syncToInstances(std::vector<RenderInstance>& instances) const
{
    if (agents_.empty()) return;

    for (RenderInstance& inst : instances) {
        for (const EnemyAgent& agent : agents_) {
            if (inst.entityId != agent.entityId) continue;
            if (!agent.alive) {
                inst.visible = false;
                break;
            }
            inst.position.x = agent.x;
            inst.position.y = agent.y;
            inst.position.z = agent.z;
            inst.yawDeg = agent.yawDeg;
            break;
        }
    }
}

void EnemySimulation3D::logAgentPositions(const char* tag) const
{
    const float playerX = lastPlayerX_;
    const float playerZ = lastPlayerZ_;

    int   alive = 0;
    float minDist = 1e9f, maxDist = 0.0f, sumDist = 0.0f;
    float closestPair = 1e9f;
    std::vector<float> bearings;

    for (const EnemyAgent& a : agents_) {
        const float dx = a.x - playerX;
        const float dz = a.z - playerZ;
        const float dPlayer = std::sqrt(dx * dx + dz * dz);
        // Bearing around the player: the spread of these is the formation.
        const float bearing = headingYawDeg(dx, dz, 0.0f);

        float nearest = 1e9f;
        for (const EnemyAgent& b : agents_) {
            if (!b.alive || b.entityId == a.entityId) continue;
            const float ex = a.x - b.x;
            const float ez = a.z - b.z;
            nearest = std::min(nearest, std::sqrt(ex * ex + ez * ez));
        }

        std::printf("[Sim3D] %s entity=%llu %-9s pos=(%.3f, %.3f, %.3f) yaw=%6.1f hp=%3d "
                    "state=%-6s slot=%2d anim=%-6s dPlayer=%.2f bearing=%7.1f dNearest=%.2f\n",
                    tag, static_cast<unsigned long long>(a.entityId), a.name.c_str(),
                    a.x, a.y, a.z, a.yawDeg, a.health, stateName(a.state), a.slot,
                    a.anim.currentState().empty() ? "-" : a.anim.currentState().c_str(),
                    dPlayer, bearing, nearest >= 1e8f ? 0.0f : nearest);

        if (!a.alive) continue;
        ++alive;
        minDist = std::min(minDist, dPlayer);
        maxDist = std::max(maxDist, dPlayer);
        sumDist += dPlayer;
        bearings.push_back(bearing < 0.0f ? bearing + 360.0f : bearing);
        if (nearest < closestPair) closestPair = nearest;
    }

    // Widest empty arc around the player: 360 with one agent, ~360/n when the
    // pack is evenly spread, and close to 360 when everybody piles on one side.
    float maxGap = 360.0f;
    if (bearings.size() > 1) {
        std::sort(bearings.begin(), bearings.end());
        maxGap = bearings.front() + 360.0f - bearings.back();
        for (size_t i = 1; i < bearings.size(); ++i) {
            maxGap = std::max(maxGap, bearings[i] - bearings[i - 1]);
        }
    }

    std::printf("[Sim3D] %s formation player=(%.2f, %.2f) alive=%d "
                "dPlayer[min/avg/max]=%.2f/%.2f/%.2f closestPair=%.2f (min allowed %.2f) "
                "maxBearingGap=%.1f maxHeightJump=%.3f cliffBlocked=%llu cliffJumps=%llu "
                "playerHp=%d/%d\n",
                tag, playerX, playerZ, alive,
                alive ? minDist : 0.0f,
                alive ? sumDist / static_cast<float>(alive) : 0.0f,
                alive ? maxDist : 0.0f,
                closestPair >= 1e8f ? 0.0f : closestPair, kMinSeparation,
                maxGap, maxHeightJump_,
                static_cast<unsigned long long>(cliffBlockedSteps_),
                static_cast<unsigned long long>(cliffJumpEvents_),
                playerHealth_, playerMaxHealth_);
}

} // namespace dash::runtime3d
