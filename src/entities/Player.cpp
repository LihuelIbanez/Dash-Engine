#include "Player.h"
#include "World.h"
#include <cmath>
#include <algorithm>

// Max HP per class
static int classMaxHp(CharacterClass cls)
{
    switch (cls) {
    case CharacterClass::Warrior: return 120;
    case CharacterClass::Mage:    return  70;
    case CharacterClass::Rogue:   return  90;
    case CharacterClass::Archer:  return  85;
    default:                      return 100;
    }
}

// Colour per class (darker, Diablo-style)
static SDL_Color classColor(CharacterClass cls)
{
    switch (cls) {
    case CharacterClass::Warrior: return {  80, 100, 200, 255 };
    case CharacterClass::Mage:    return { 140,  30, 200, 255 };
    case CharacterClass::Rogue:   return {  30, 150,  60, 255 };
    case CharacterClass::Archer:  return { 180, 140,  20, 255 };
    default:                      return {  70, 120, 200, 255 };
    }
}

Player::Player(float startX, float startY, CharacterClass cls)
    : Character(startX, startY, cls, classMaxHp(cls), "Player")
{}

// ─── Input ────────────────────────────────────────────────────────────────────
void Player::handleInput(const Uint8* keys, float dt)
{
    // WASD still works as fallback
    float kx = 0.f, ky = 0.f;
    if (keys[SDL_SCANCODE_W]) { kx -= 1.f; ky -= 1.f; }
    if (keys[SDL_SCANCODE_S]) { kx += 1.f; ky += 1.f; }
    if (keys[SDL_SCANCODE_A]) { kx -= 1.f; ky += 1.f; }
    if (keys[SDL_SCANCODE_D]) { kx += 1.f; ky -= 1.f; }

    if (kx != 0.f || ky != 0.f) {
        float len = std::sqrt(kx * kx + ky * ky);
        vx = kx / len;
        vy = ky / len;
        hasTarget = false;   // keyboard overrides mouse target
    } else if (hasTarget) {
        float dx = targetX - x;
        float dy = targetY - y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 0.15f) {
            hasTarget = false;
            vx = 0.f; vy = 0.f;
        } else {
            vx = dx / dist;
            vy = dy / dist;
        }
    } else {
        vx = 0.f; vy = 0.f;
    }

    // SPACE / ENTER attack (keyboard)
    if ((keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_RETURN]) && canAttack()) {
        triggerAttack();
    }

    (void)dt;
}

void Player::setMoveTarget(float wx, float wy)
{
    targetX   = wx;
    targetY   = wy;
    hasTarget = true;
}

void Player::triggerAttack()
{
    if (!canAttack()) return;
    constexpr int MANA_COST = 5;
    if (mana < MANA_COST) return;
    isAttacking    = true;
    attackCooldown = attackCooldownMax;
    mana           = std::max(0, mana - MANA_COST);
}

void Player::update(float dt)
{
    tickCooldowns(dt);

    x += vx * stats.speed * dt;
    y += vy * stats.speed * dt;

    x = std::clamp(x, 0.5f, static_cast<float>(WORLD_W) - 1.5f);
    y = std::clamp(y, 0.5f, static_cast<float>(WORLD_H) - 1.5f);

    // Snap to terrain height
    if (world_) {
        z = world_->terrain().sampleHeight(x, y);
    }
}

// ─── Draw ─────────────────────────────────────────────────────────────────────
void Player::draw(SDL_Renderer* renderer, float camX, float camY) const
{
    Vec2f s = worldToScreen(x, y, camX, camY);
    float sx = s.x, sy = s.y;

    // Offset for terrain height
    const float heightPixels = TILE_SCALE * 32.0f;  // matches editor heightScale
    sy -= z * heightPixels;

    SDL_Color col  = classColor(charClass);
    SDL_Color dark = colDarken(col, 60);
    SDL_Color lite = { static_cast<Uint8>(std::min(255, col.r + 60)),
                       static_cast<Uint8>(std::min(255, col.g + 60)),
                       static_cast<Uint8>(std::min(255, col.b + 60)), 255 };

    // Ground shadow
    {
        const float hw = 16.f, hh = 8.f;
        SDL_Vertex v[4] = {
            { {sx,      sy - hh}, {0,0,0,120}, {0,0} },
            { {sx + hw, sy     }, {0,0,0,120}, {0,0} },
            { {sx,      sy + hh}, {0,0,0,120}, {0,0} },
            { {sx - hw, sy     }, {0,0,0,120}, {0,0} },
        };
        const int idx[6] = {0,1,3, 1,2,3};
        SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
    }

    // Legs (small diamond at feet)
    {
        const float lw = 8.f, lh = 6.f;
        float bx = sx, by = sy - 6.f;
        SDL_Vertex v[4] = {
            { {bx,      by - lh}, dark, {0,0} },
            { {bx + lw, by     }, dark, {0,0} },
            { {bx,      by + lh}, dark, {0,0} },
            { {bx - lw, by     }, dark, {0,0} },
        };
        const int idx[6] = {0,1,3, 1,2,3};
        SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
    }

    // Body – upright diamond
    {
        const float bw = 12.f, bh = 20.f;
        float bx = sx, by = sy - 20.f;
        SDL_Vertex v[4] = {
            { {bx,      by - bh}, lite, {0,0} },
            { {bx + bw, by     }, col,  {0,0} },
            { {bx,      by + bh * 0.35f}, dark, {0,0} },
            { {bx - bw, by     }, col,  {0,0} },
        };
        const int idx[6] = {0,1,3, 1,2,3};
        SDL_RenderGeometry(renderer, nullptr, v, 4, idx, 6);
    }

    // Head (small bright circle approximated as filled rect)
    SDL_SetRenderDrawColor(renderer, lite.r, lite.g, lite.b, 255);
    SDL_FRect head { sx - 5.f, sy - 46.f, 10.f, 10.f };
    SDL_RenderFillRectF(renderer, &head);

    // Attack flash – white ring
    if (isAttacking) {
        SDL_SetRenderDrawColor(renderer, 255, 220, 80, 200);
        for (int ring = 16; ring <= 24; ring += 4) {
            SDL_FRect r { sx - ring, sy - ring, ring * 2.f, ring * 2.f };
            SDL_RenderDrawRectF(renderer, &r);
        }
    }

    // Move target indicator
    if (hasTarget) {
        Vec2f ts = worldToScreen(targetX, targetY, camX, camY);
        SDL_SetRenderDrawColor(renderer, 255, 255, 100, 120);
        SDL_FRect cross1 { ts.x - 6.f, ts.y - 1.f, 12.f, 2.f };
        SDL_FRect cross2 { ts.x - 1.f, ts.y - 6.f, 2.f, 12.f };
        SDL_RenderFillRectF(renderer, &cross1);
        SDL_RenderFillRectF(renderer, &cross2);
    }

    drawHealthBar(renderer, sx, sy);
}
