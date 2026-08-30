#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "game/nav/GridNav.h"
#include "game/runtime3d/AgentAI.h"
#include "game/runtime3d/AgentAnimation.h"
#include "rendering/vulkan/RenderTypes.h"

struct SceneData;
class World;
class TerrainMesh;
class EventDispatcher;

namespace dash::runtime3d {

using RenderInstance = dash::vkexp::RenderInstance;

// One simulated enemy. Positions are in tile space (x, z) like PlayerController,
// with `y` resolved from the terrain every frame so agents walk the same ground.
struct EnemyAgent {
    uint64_t    entityId = 0;
    std::string name;

    float x = 0.0f;
    float z = 0.0f;
    float y = 0.0f;
    // Distance kept above the sampled ground, captured from the authored pose.
    float groundOffset = 0.0f;
    float yawDeg = 0.0f;

    int  health = 0;
    bool alive  = true;

    AgentStats  stats{};
    AgentTimers timers{};
    AgentState  state = AgentState::Idle;

    float attackCooldown = 0.0f;

    // Patrol beat: waypoints on a ring around where the agent was placed.
    float    spawnX = 0.0f;
    float    spawnZ = 0.0f;
    float    patrolTargetX = 0.0f;
    float    patrolTargetZ = 0.0f;
    uint32_t patrolSeq = 0;

    // Standing spot around the player while engaged; -1 when not surrounding.
    int slot = -1;

    // Driven by the FSM, consumed by whoever plays clips (see AgentAnimation.h).
    dash::anim::StateMachineRuntime anim;
    float animStateTime = 0.0f;
    float planarSpeed = 0.0f;
    bool  swungThisFrame = false;
    bool  diedThisFrame = false;

    std::vector<NavPoint> path;
    int   pathIndex = 0;
    float pathAge   = 0.0f;
};

// Drives every Enemy entity of a scene: A* on the 2D tile grid for planning,
// terrain sampling for height, and melee combat against the player.
class EnemySimulation3D {
public:
    EnemySimulation3D();
    ~EnemySimulation3D();

    EnemySimulation3D(const EnemySimulation3D&) = delete;
    EnemySimulation3D& operator=(const EnemySimulation3D&) = delete;

    // Builds one agent per Enemy entity, seeding the nav grid from the scene's
    // worldSeed so pathfinding agrees with the 3D terrain. Instances must
    // already be grounded (snapInstancesToTerrain) when this runs.
    void build(const SceneData& scene, const std::vector<RenderInstance>& instances);

    bool empty() const { return agents_.empty(); }
    size_t agentCount() const { return agents_.size(); }
    const std::vector<EnemyAgent>& agents() const { return agents_; }

    // Advances AI, movement and combat. Does nothing when dt <= 0 so the
    // play-mode pause of EditorBridge freezes the simulation too.
    void update(float dt, float playerX, float playerZ,
                const TerrainMesh* terrainMesh, bool terrainMeshReady,
                EventDispatcher& events);

    // Writes simulated poses back into the render instances, matched by entity
    // id because resolveSceneMeshes() reorders the instance vector.
    void syncToInstances(std::vector<RenderInstance>& instances) const;

    int  playerHealth() const { return playerHealth_; }
    bool playerAlive() const { return playerHealth_ > 0; }

    void setPlayerEntityId(uint64_t id) { playerEntityId_ = id; }

    // Behaviour evidence for the smoke run: formation, spacing and the cliff
    // guard counters, one line per living agent.
    void logAgentPositions(const char* tag) const;

private:
    void refreshPath(EnemyAgent& agent, float targetX, float targetZ,
                     const TerrainMesh* terrain);
    void assignSlots(float playerX, float playerZ);
    // Applies a step, refusing (and then sliding along) moves that would cross a
    // cliff wall.
    void moveAgent(EnemyAgent& agent, float stepX, float stepZ,
                   const TerrainMesh* terrain);
    void resolveCombat(float dt, float playerX, float playerZ, EventDispatcher& events);

    std::vector<EnemyAgent> agents_;
    std::unique_ptr<World>  navWorld_;
    bool navWorldReady_ = false;
    // The authored Y only becomes a ground offset once the renderer hands us
    // the terrain it actually drew, which happens on the first update().
    bool groundResolved_ = false;

    SurroundRings rings_{};
    float slotTimer_ = 0.0f;

    float lastPlayerX_ = 0.0f;
    float lastPlayerZ_ = 0.0f;

    // Cliff telemetry: steps vetoed, and the worst per-frame vertical delta any
    // agent took. A jump of a full CLIFF_STEP would mean the guard leaked.
    uint64_t cliffBlockedSteps_ = 0;
    uint64_t cliffJumpEvents_ = 0;
    float    maxHeightJump_ = 0.0f;

    uint64_t playerEntityId_ = 0;
    int      playerHealth_ = 100;
    int      playerMaxHealth_ = 100;
    int      playerAttack_ = 14;
    float    playerAttackRadius_ = 1.6f;
    float    playerAttackCooldownMax_ = 0.8f;
    float    playerAttackCooldown_ = 0.0f;
};

} // namespace dash::runtime3d
