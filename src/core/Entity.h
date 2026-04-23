#pragma once
#include <string>
#include <SDL2/SDL.h>
#include "IsoRenderer.h"

// ─────────────────────────────────────────────────────────────────────────────
// Entity – base for every game object that lives in the world
// ─────────────────────────────────────────────────────────────────────────────
class Entity {
public:
    // World-space position (float for smooth sub-tile movement)
    float x, y;
    float z = 0.0f;   // terrain height (set by gravity / terrain snapping)

    int         health;
    int         maxHealth;
    std::string name;
    bool        alive;

    Entity(float x, float y, int maxHp, const std::string& name);
    virtual ~Entity() = default;

    // --- interface -----------------------------------------------------------
    virtual void update(float dt) = 0;
    virtual void draw(SDL_Renderer* renderer, float camX, float camY) const = 0;

    // --- helpers -------------------------------------------------------------
    void takeDamage(int amount);
    bool isAlive()   const { return alive && health > 0; }

    // Draw a small health bar above the entity
    void drawHealthBar(SDL_Renderer* renderer, float sx, float sy) const;
};
