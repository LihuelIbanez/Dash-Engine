#include "Enemy.h"
#include "GameplayDatabase.h"
#include "GridNav.h"
#include "World.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <cstdio>

Enemy::Enemy(float x, float y, const std::string& name)
    : Character(x, y, CharacterClass::Enemy, 60, name)
    , detectionRadius(6.f)
    , attackRadius(1.2f)
    , expReward(40)
{
    stats.speed = 2.2f;
    idleTimer_  = 1.5f + static_cast<float>(std::rand() % 200) / 100.f;
    pickNewPatrolDir();
}

Enemy::Enemy(float x, float y, const EnemyData& data)
    : Character(x, y, CharacterClass::Enemy, data.maxHp, data.name)
    , detectionRadius(data.detectionRadius)
    , attackRadius(data.attackRadius)
    , expReward(data.expReward)
{
    stats.attack      = data.attack;
    stats.defense     = data.defense;
    stats.magicAttack = data.magicAttack;
    stats.speed       = data.speed;
    stats.critChance  = data.critChance;
    attackCooldownMax = data.attackCooldown;
    idleTimer_  = 1.5f + static_cast<float>(std::rand() % 200) / 100.f;
    pickNewPatrolDir();
}

float Enemy::distTo(float px, float py) const
{
    float dx = px - x, dy = py - y;
    return std::sqrt(dx * dx + dy * dy);
}

void Enemy::pickNewPatrolDir()
{
    // Pick a random world-space direction
    float angle  = static_cast<float>(std::rand()) / RAND_MAX * 6.2832f;
    patrolDirX_  = std::cos(angle);
    patrolDirY_  = std::sin(angle);
    patrolTimer_ = 1.0f + static_cast<float>(std::rand() % 200) / 100.f;
}

// ─── AI update ────────────────────────────────────────────────────────────────
void Enemy::updateAI(float dt, float playerX, float playerY, const World* world)
{
    movementWorld_ = world;

    // Process cooldowns at the beginning of the AI phase so any attack trigger
    // remains visible to CombatSystem during this frame.
    tickCooldowns(dt);

    float dist = distTo(playerX, playerY);
    vx = 0.f;  vy = 0.f;

    switch (state_) {
    // ── Idle ──────────────────────────────────────────────────────────────────
    case EnemyState::Idle:
        idleTimer_ -= dt;
        if (idleTimer_ <= 0.f) {
            state_      = EnemyState::Patrol;
            pickNewPatrolDir();
        }
        if (dist < detectionRadius) {
            state_ = EnemyState::Chase;
            path_.clear();
            pathIdx_ = 0;
            pathRefreshT_ = 99.f; // force immediate pathfind
        }
        break;

    // ── Patrol ────────────────────────────────────────────────────────────────
    case EnemyState::Patrol:
        patrolTimer_ -= dt;
        vx = patrolDirX_ * 0.5f;   // slow patrol
        vy = patrolDirY_ * 0.5f;
        if (patrolTimer_ <= 0.f) {
            state_      = EnemyState::Idle;
            idleTimer_  = 1.f + static_cast<float>(std::rand() % 100) / 100.f;
        }
        if (dist < detectionRadius) {
            state_ = EnemyState::Chase;
            path_.clear();
            pathIdx_ = 0;
            pathRefreshT_ = 99.f;
        }
        break;

    // ── Chase (A* pathfinding) ────────────────────────────────────────────────
    case EnemyState::Chase: {
        // Refresh path every ~0.5 seconds or if path was cleared
        pathRefreshT_ += dt;
        if (world && (pathRefreshT_ >= 0.5f || path_.empty())) {
            pathRefreshT_ = 0.f;
            NavPoint start = GridNav::worldToTile(x, y);
            NavPoint goal  = GridNav::worldToTile(playerX, playerY);
            path_ = GridNav::findPath(start.x, start.y,
                                      goal.x,  goal.y, *world);
            pathIdx_ = 0;
            // Skip the first waypoint if we're already on it
            if (path_.size() > 1) pathIdx_ = 1;
        }

        // Follow the current waypoint
        if (!path_.empty() && pathIdx_ < static_cast<int>(path_.size())) {
            float wpx, wpy;
            GridNav::tileToCentre(path_[pathIdx_].x, path_[pathIdx_].y, wpx, wpy);
            float dx = wpx - x, dy = wpy - y;
            float wdist = std::sqrt(dx * dx + dy * dy);
            if (wdist < 0.3f) {
                // Reached waypoint, advance to next
                ++pathIdx_;
            }
            if (pathIdx_ < static_cast<int>(path_.size())) {
                GridNav::tileToCentre(path_[pathIdx_].x, path_[pathIdx_].y, wpx, wpy);
                dx = wpx - x; dy = wpy - y;
                wdist = std::sqrt(dx * dx + dy * dy);
                if (wdist > 1e-3f) {
                    vx = dx / wdist;
                    vy = dy / wdist;
                }
            }
        } else {
            // Fallback: direct chase if no path (shouldn't happen often)
            float dx = playerX - x, dy = playerY - y;
            float len = std::sqrt(dx * dx + dy * dy);
            if (len > 1e-3f) { vx = dx / len;  vy = dy / len; }
        }

        if (dist <= attackRadius) {
            state_ = EnemyState::Attack;
            path_.clear();
        }
        if (dist > detectionRadius * 1.5f) {
            state_ = EnemyState::Idle;
            path_.clear();
        }
        break;
    }

    // ── Attack ────────────────────────────────────────────────────────────────
    case EnemyState::Attack:
        if (canAttack()) {
            isAttacking    = true;
            attackCooldown = attackCooldownMax;
        }
        if (dist > attackRadius * 1.3f)       state_ = EnemyState::Chase;
        if (dist > detectionRadius * 1.5f)    state_ = EnemyState::Idle;
        break;
    }
}

void Enemy::update(float dt)
{
    // Physics-style movement: accelerate toward AI intent and apply damping.
    auto approach = [](float current, float target, float maxDelta) {
        const float delta = target - current;
        if (delta > maxDelta) return current + maxDelta;
        if (delta < -maxDelta) return current - maxDelta;
        return target;
    };

    const float desiredVx = vx * stats.speed;
    const float desiredVy = vy * stats.speed;
    const float maxDelta = std::max(0.0f, moveAccel_ * dt);
    physVelX_ = approach(physVelX_, desiredVx, maxDelta);
    physVelY_ = approach(physVelY_, desiredVy, maxDelta);

    // Extra drag when no active movement intent.
    if (std::fabs(vx) < 1e-3f) {
        physVelX_ *= std::exp(-moveDamping_ * dt);
    }
    if (std::fabs(vy) < 1e-3f) {
        physVelY_ *= std::exp(-moveDamping_ * dt);
    }

    float nextX = x + physVelX_ * dt;
    float nextY = y + physVelY_ * dt;
    bool blockedX = false;
    bool blockedY = false;

    if (movementWorld_) {
        const bool currentWalkable = movementWorld_->isWalkable(x, y);

        // Resolve axis independently to allow sliding along walls.
        if (!currentWalkable || movementWorld_->isWalkable(nextX, y)) {
            x = nextX;
        } else {
            physVelX_ = 0.f;
            blockedX = true;
        }

        if (!currentWalkable || movementWorld_->isWalkable(x, nextY)) {
            y = nextY;
        } else {
            physVelY_ = 0.f;
            blockedY = true;
        }
    } else {
        x = nextX;
        y = nextY;
    }

    x = std::clamp(x, 0.5f, static_cast<float>(WORLD_W) - 1.5f);
    y = std::clamp(y, 0.5f, static_cast<float>(WORLD_H) - 1.5f);

    // Snap to terrain height
    if (movementWorld_) {
        z = movementWorld_->terrain().sampleHeight(x, y);
    }

    if (stuckLogCooldown_ > 0.f) stuckLogCooldown_ = std::max(0.f, stuckLogCooldown_ - dt);
    const bool hasMoveIntent = (std::fabs(vx) + std::fabs(vy)) > 0.2f;
    if (hasMoveIntent && blockedX && blockedY && stuckLogCooldown_ <= 0.f) {
        std::printf("[WARN] Enemy movement blocked: name=%s pos=(%.2f,%.2f) intent=(%.2f,%.2f)\n",
                    name.c_str(), x, y, vx, vy);
        stuckLogCooldown_ = 2.0f;
    }
}

// ─── Draw ─────────────────────────────────────────────────────────────────────
void Enemy::draw(SDL_Renderer* renderer, float camX, float camY) const
{
    Vec2f s = worldToScreen(x, y, camX, camY);
    float sx = s.x, sy = s.y;

    // Offset for terrain height
    const float heightPixels = TILE_SCALE * 32.0f;
    sy -= z * heightPixels;

    // Skeleton-like palette: bone white body with dark joints
    SDL_Color bodyCol = isAttacking
        ? SDL_Color{255, 80,  40, 255}   // glowing red when attacking
        : SDL_Color{180, 170, 140, 255}; // bone
    SDL_Color darkCol = isAttacking
        ? SDL_Color{160, 20, 10, 255}
        : SDL_Color{ 80,  70,  50, 255}; // dark bone joint

    // Shadow
    {
        const float hw = 16.f, hh = 8.f;
        SDL_Vertex v[4] = {
            { {sx,      sy - hh}, {0,0,0,110}, {0,0} },
            { {sx + hw, sy     }, {0,0,0,110}, {0,0} },
            { {sx,      sy + hh}, {0,0,0,110}, {0,0} },
            { {sx - hw, sy     }, {0,0,0,110}, {0,0} },
        };
        const int idx[6] = {0,1,3, 1,2,3};
        SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
    }

    // Legs (thin lower diamond)
    {
        const float lw = 7.f, lh = 7.f;
        float bx = sx, by = sy - 5.f;
        SDL_Vertex v[4] = {
            { {bx,      by - lh}, darkCol, {0,0} },
            { {bx + lw, by     }, darkCol, {0,0} },
            { {bx,      by + lh}, darkCol, {0,0} },
            { {bx - lw, by     }, darkCol, {0,0} },
        };
        const int idx[6] = {0,1,3, 1,2,3};
        SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
    }

    // Body (stocky ribcage diamond)
    {
        const float bw = 14.f, bh = 16.f;
        float bx = sx, by = sy - 18.f;
        SDL_Vertex v[4] = {
            { {bx,      by - bh}, bodyCol, {0,0} },
            { {bx + bw, by     }, darkCol, {0,0} },
            { {bx,      by + bh * 0.4f}, darkCol, {0,0} },
            { {bx - bw, by     }, darkCol, {0,0} },
        };
        const int idx[6] = {0,1,3, 1,2,3};
        SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
    }

    // Skull (slightly angular rect)
    SDL_Color skullCol = isAttacking
        ? SDL_Color{255, 120, 60, 255}
        : SDL_Color{210, 200, 175, 255};
    SDL_SetRenderDrawColor(renderer, skullCol.r, skullCol.g, skullCol.b, 255);
    SDL_FRect skull { sx - 6.f, sy - 40.f, 12.f, 10.f };
    SDL_RenderFillRectF(renderer, &skull);
    // Eye sockets
    SDL_SetRenderDrawColor(renderer, 10, 0, 0, 255);
    SDL_FRect eye1 { sx - 5.f, sy - 39.f, 3.f, 3.f };
    SDL_FRect eye2 { sx + 2.f, sy - 39.f, 3.f, 3.f };
    SDL_RenderFillRectF(renderer, &eye1);
    SDL_RenderFillRectF(renderer, &eye2);

    // State indicator dot
    SDL_Color dotCol = {200, 0, 0, 255};
    switch (state_) {
    case EnemyState::Chase:  dotCol = {255, 160,   0, 255}; break;
    case EnemyState::Attack: dotCol = {255,  60,  60, 255}; break;
    case EnemyState::Patrol: dotCol = {160,  80,  40, 255}; break;
    default: break;
    }
    SDL_SetRenderDrawColor(renderer, dotCol.r, dotCol.g, dotCol.b, dotCol.a);
    SDL_FRect dot { sx - 3.f, sy - 48.f, 6.f, 6.f };
    SDL_RenderFillRectF(renderer, &dot);

    drawHealthBar(renderer, sx, sy);
}

