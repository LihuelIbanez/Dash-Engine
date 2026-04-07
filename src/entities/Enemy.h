#pragma once
#include "Character.h"

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

    // Call each frame with the player's current world position
    void updateAI(float dt, float playerX, float playerY);

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

    void pickNewPatrolDir();
    float distTo(float px, float py) const;
};
