#pragma once
#include "Character.h"

class World;  // forward declaration for terrain snapping

// ─────────────────────────────────────────────────────────────────────────────
// Player – controlled by keyboard + mouse (Diablo-style point & click)
// ─────────────────────────────────────────────────────────────────────────────
class Player : public Character {
public:
    // Selected class is set at construction; can be changed in a menu later
    explicit Player(float startX, float startY, CharacterClass cls = CharacterClass::Warrior);

    // Call once per frame before update()
    void handleInput(const Uint8* keys, float dt);

    // Process a right-click or left-click at world coordinates
    void setMoveTarget(float wx, float wy);
    void triggerAttack();

    void update(float dt)                                        override;
    void draw(SDL_Renderer* renderer, float camX, float camY) const override;

    // Returns true if the player just triggered an attack this frame
    bool attackedThisFrame() const { return isAttacking; }

    // Mouse-driven movement target
    float targetX = -1.f;
    float targetY = -1.f;
    bool  hasTarget = false;

    // Terrain snapping
    void setWorld(const World* w) { world_ = w; }

private:
    const World* world_ = nullptr;
};
