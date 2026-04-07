#pragma once
#include "Character.h"
#include "GridNav.h"
#include <vector>

struct EnemyData;   // forward declaration (src/game/data/GameplayDatabase.h)

// ─────────────────────────────────────────────────────────────────────────────
// Enemy AI behaviour states
// ─────────────────────────────────────────────────────────────────────────────
enum class EnemyState {
    Idle,       // standing still
    Patrol,     // wandering randomly
    Chase,      // detected player – moving toward target
    Attack      // close enough to hit
};

// ─────────────────────────────────────────────────────────────────────────────
// Enemy – driven by a simple finite state machine
// ─────────────────────────────────────────────────────────────────────────────
class Enemy : public Character {
public:
    float detectionRadius;  // world-units; player triggers Chase
    float attackRadius;     // world-units; trigger Attack
    int   expReward;        // XP granted to player on death

    Enemy(float x, float y, const std::string& name = "Goblin");

    // Data-driven constructor: initialise from EnemyData loaded from JSON
    Enemy(float x, float y, const EnemyData& data);

    // Call each frame with the player's current world position
    void updateAI(float dt, float playerX, float playerY,
                  const class World* world = nullptr);

    void update(float dt)                                        override;
    void draw(SDL_Renderer* renderer, float camX, float camY) const override;

    // Returns true when the enemy just executed an attack
    bool attackedThisFrame() const { return isAttacking; }

private:
    EnemyState state_      = EnemyState::Idle;
    float      idleTimer_  = 0.f;
    float      patrolDirX_ = 0.f;
    float      patrolDirY_ = 0.f;
    float      patrolTimer_= 0.f;

    // A* path cache
    std::vector<NavPoint> path_;
    int                   pathIdx_       = 0;
    float                 pathRefreshT_  = 0.f; // time since last pathfind

    void pickNewPatrolDir();
    float distTo(float px, float py) const;
};
