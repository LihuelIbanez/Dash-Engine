#pragma once
#include <SDL2/SDL.h>
#include <vector>
#include <memory>
#include "World.h"
#include "Player.h"
#include "Enemy.h"

// ─────────────────────────────────────────────────────────────────────────────
// Game – owns the SDL window/renderer and the game loop
// ─────────────────────────────────────────────────────────────────────────────
class Game {
public:
    Game();
    ~Game();

    bool init();
    void run();

private:
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;

    bool          running_  = false;
    World         world_;
    Player        player_;
    std::vector<std::unique_ptr<Enemy>> enemies_;

    int           score_    = 0;

    // Diablo 2 HUD panel height (bottom strip)
    static constexpr int HUD_H       = 100;
    // Orb radius
    static constexpr int ORB_R       = 40;

    // Viewport height for game world (excludes HUD)
    int gameViewH() const { return SCREEN_H - HUD_H; }

    // ── Screen → world (for mouse picking) ──────────────────────────────────
    bool screenToWorld(int mx, int my, float& wx, float& wy) const;

    // --- loop phases ---------------------------------------------------------
    void processEvents();
    void update(float dt);
    void render();

    // --- combat --------------------------------------------------------------
    void resolveAttacks();

    // --- HUD (Diablo 2 style) ------------------------------------------------
    void renderHUD();
    void renderOrb(int cx, int cy, int radius, float ratio,
                   SDL_Color fillCol, SDL_Color bgCol, const char* label);
    void renderString(int x, int y, const char* text, SDL_Color col, int scale = 2);
    void renderChar(int px, int py, char c, SDL_Color col, int scale);
};
