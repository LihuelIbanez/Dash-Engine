#include "Game.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include "Font5x7.h"

// ─────────────────────────────────────────────────────────────────────────────
// Font helpers (member versions so they can use renderer_)
// ─────────────────────────────────────────────────────────────────────────────
void Game::renderChar(int px, int py, char c, SDL_Color col, int scale)
{
    if (c < 32 || c > 126) return;
    const uint8_t* glyph = font5x7[c - 32];
    SDL_SetRenderDrawColor(renderer_, col.r, col.g, col.b, col.a);
    for (int cx = 0; cx < 5; ++cx) {
        uint8_t bits = glyph[cx];
        for (int row = 0; row < 7; ++row) {
            if (bits & (1 << row)) {
                SDL_Rect rect { px + cx * scale, py + row * scale, scale, scale };
                SDL_RenderFillRect(renderer_, &rect);
            }
        }
    }
}

void Game::renderString(int x, int y, const char* text, SDL_Color col, int scale)
{
    int cx = x;
    while (*text) {
        renderChar(cx, y, *text, col, scale);
        cx += (5 + 1) * scale;
        ++text;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Screen → World (inverse isometric projection, using player as camera)
// ─────────────────────────────────────────────────────────────────────────────
bool Game::screenToWorld(int mx, int my, float& wx, float& wy) const
{
    // Only valid in the game viewport (above HUD panel)
    if (my >= gameViewH()) return false;

    float camX = player_.x;
    float camY = player_.y;

    // Undo: sx = (rx-ry)*TW/2 + SW/2  →  rx-ry = (sx-SW/2)*2/TW
    //       sy = (rx+ry)*TH/2 + SH/2  →  rx+ry = (sy-SH/2)*2/TH
    float sx = static_cast<float>(mx);
    float sy = static_cast<float>(my);

    float u = (sx - SCREEN_W * 0.5f) * 2.f / TILE_W;  // rx - ry
    float v = (sy - SCREEN_H * 0.5f) * 2.f / TILE_H;  // rx + ry

    float rx = (u + v) * 0.5f;
    float ry = (v - u) * 0.5f;

    wx = rx + camX;
    wy = ry + camY;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────
Game::Game()
    : player_(static_cast<float>(WORLD_W) / 2.f,
              static_cast<float>(WORLD_H) / 2.f,
              CharacterClass::Warrior)
{
    std::srand(12345);
    world_.generate(12345);

    // Spawn several goblins around the player
    const float cx = static_cast<float>(WORLD_W) / 2.f;
    const float cy = static_cast<float>(WORLD_H) / 2.f;

    auto addEnemy = [&](float ox, float oy, const std::string& n) {
        enemies_.push_back(std::make_unique<Enemy>(cx + ox, cy + oy, n));
    };
    addEnemy( 4.f,  3.f, "Skeleton");
    addEnemy(-5.f,  2.f, "Zombie");
    addEnemy( 2.f, -5.f, "Skeleton");
    addEnemy(-3.f, -4.f, "Fallen");
    addEnemy( 6.f, -2.f, "Zombie");
}

Game::~Game()
{
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_)   SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool Game::init()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    window_ = SDL_CreateWindow(
        "Diablo RPG",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H,
        SDL_WINDOW_SHOWN
    );
    if (!window_) return false;

    renderer_ = SDL_CreateRenderer(window_, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) return false;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    running_ = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main loop
// ─────────────────────────────────────────────────────────────────────────────
void Game::run()
{
    Uint64 prev = SDL_GetPerformanceCounter();
    Uint64 freq = SDL_GetPerformanceFrequency();

    while (running_) {
        Uint64 now = SDL_GetPerformanceCounter();
        float  dt  = static_cast<float>(now - prev) / static_cast<float>(freq);
        prev = now;
        if (dt > 0.05f) dt = 0.05f;

        processEvents();
        update(dt);
        render();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Events — including Diablo-style mouse click to move / attack
// ─────────────────────────────────────────────────────────────────────────────
void Game::processEvents()
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) { running_ = false; return; }
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
            running_ = false; return;
        }

        // Left-click: move to location OR attack enemy
        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) {
            float wx, wy;
            if (screenToWorld(ev.button.x, ev.button.y, wx, wy)) {
                // Check if an enemy is near the click
                bool hitEnemy = false;
                for (auto& e : enemies_) {
                    if (!e->isAlive()) continue;
                    float dx = e->x - wx, dy = e->y - wy;
                    if (std::sqrt(dx*dx + dy*dy) < 1.0f) {
                        // Click on enemy: move towards it and attack
                        player_.setMoveTarget(e->x, e->y);
                        hitEnemy = true;
                        break;
                    }
                }
                if (!hitEnemy) {
                    player_.setMoveTarget(wx, wy);
                }
            }
        }

        // Right-click: attack in place
        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_RIGHT) {
            player_.triggerAttack();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Update
// ─────────────────────────────────────────────────────────────────────────────
void Game::update(float dt)
{
    if (!player_.isAlive()) { running_ = false; return; }

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    player_.handleInput(keys, dt);
    player_.update(dt);

    // Auto-attack nearby enemy when close enough (Diablo behaviour)
    if (player_.hasTarget) {
        for (auto& e : enemies_) {
            if (!e->isAlive()) continue;
            float dx = e->x - player_.x, dy = e->y - player_.y;
            if (std::sqrt(dx*dx + dy*dy) < 1.2f && player_.canAttack()) {
                player_.triggerAttack();
                break;
            }
        }
    }

    for (auto& e : enemies_) {
        if (!e->isAlive()) continue;
        e->updateAI(dt, player_.x, player_.y);
        e->update(dt);
    }

    resolveAttacks();

    for (auto& e : enemies_) {
        if (!e->isAlive() && e->health == 0) {
            player_.gainExperience(e->expReward);
            score_ += 10;
            e->health = -1;
        }
    }
    enemies_.erase(
        std::remove_if(enemies_.begin(), enemies_.end(),
            [](const std::unique_ptr<Enemy>& e){ return e->health < 0; }),
        enemies_.end()
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// Combat
// ─────────────────────────────────────────────────────────────────────────────
void Game::resolveAttacks()
{
    if (player_.attackedThisFrame()) {
        for (auto& e : enemies_) {
            if (!e->isAlive()) continue;
            float dx = e->x - player_.x, dy = e->y - player_.y;
            if (std::sqrt(dx*dx + dy*dy) <= 1.8f) {
                int dmg = player_.rollDamage() - e->stats.defense;
                e->takeDamage(std::max(1, dmg));
            }
        }
    }

    for (auto& e : enemies_) {
        if (!e->isAlive()) continue;
        if (e->attackedThisFrame()) {
            int dmg = e->rollDamage() - player_.stats.defense;
            player_.takeDamage(std::max(1, dmg));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────
void Game::render()
{
    // Very dark background — Diablo's near-black
    SDL_SetRenderDrawColor(renderer_, 8, 6, 4, 255);
    SDL_RenderClear(renderer_);

    float camX = player_.x;
    float camY = player_.y;

    world_.draw(renderer_, camX, camY);

    for (auto& e : enemies_)
        if (e->isAlive()) e->draw(renderer_, camX, camY);

    player_.draw(renderer_, camX, camY);

    renderHUD();
    SDL_RenderPresent(renderer_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Orb renderer — filled circular orb (approximated with stacked rects)
// ─────────────────────────────────────────────────────────────────────────────
void Game::renderOrb(int cx, int cy, int radius, float ratio,
                     SDL_Color fillCol, SDL_Color bgCol, const char* label)
{
    int r = radius;

    // Draw each horizontal slice of the circle
    for (int dy = -r; dy <= r; ++dy) {
        float frac = static_cast<float>(dy + r) / (2.f * r);   // 0=top, 1=bottom
        int   half = static_cast<int>(std::sqrt(static_cast<float>(r*r - dy*dy)));

        // Background (empty portion — always drawn)
        SDL_SetRenderDrawColor(renderer_, bgCol.r, bgCol.g, bgCol.b, bgCol.a);
        SDL_RenderDrawLine(renderer_,
            cx - half, cy + dy,
            cx + half, cy + dy);

        // Fill from the bottom up based on ratio
        if (frac >= (1.f - ratio)) {
            SDL_SetRenderDrawColor(renderer_, fillCol.r, fillCol.g, fillCol.b, fillCol.a);
            SDL_RenderDrawLine(renderer_,
                cx - half, cy + dy,
                cx + half, cy + dy);
        }
    }

    // Rim / edge highlight
    SDL_SetRenderDrawColor(renderer_, 180, 140, 80, 200);
    for (int dy = -r; dy <= r; ++dy) {
        int half = static_cast<int>(std::sqrt(static_cast<float>(r*r - dy*dy)));
        SDL_RenderDrawPoint(renderer_, cx - half, cy + dy);
        SDL_RenderDrawPoint(renderer_, cx + half, cy + dy);
    }

    // Label centred under orb
    int labelLen = 0;
    for (const char* p = label; *p; ++p) ++labelLen;
    int lx = cx - (labelLen * 6) / 2;
    renderString(lx, cy + r + 4, label, {200, 180, 120, 255}, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// HUD — Diablo 2 style bottom panel
// ─────────────────────────────────────────────────────────────────────────────
void Game::renderHUD()
{
    const int panelY = gameViewH();

    // ── Dark stone panel background ──────────────────────────────────────────
    SDL_SetRenderDrawColor(renderer_, 18, 12, 8, 255);
    SDL_Rect panel { 0, panelY, SCREEN_W, HUD_H };
    SDL_RenderFillRect(renderer_, &panel);

    // Top border line (golden, like D2's panel border)
    SDL_SetRenderDrawColor(renderer_, 120, 90, 40, 255);
    SDL_RenderDrawLine(renderer_, 0, panelY, SCREEN_W, panelY);
    SDL_SetRenderDrawColor(renderer_, 80, 60, 25, 255);
    SDL_RenderDrawLine(renderer_, 0, panelY + 1, SCREEN_W, panelY + 1);

    const int orbCY = panelY + HUD_H / 2;

    // ── Life orb (left side) ─────────────────────────────────────────────────
    float hpRatio = static_cast<float>(player_.health) / player_.maxHealth;
    renderOrb(ORB_R + 12, orbCY, ORB_R,
              hpRatio,
              {180, 20, 20, 255},   // blood red fill
              {40,  4,  4, 255},    // dark red bg
              "LIFE");

    // ── Mana orb (right side) ────────────────────────────────────────────────
    float manaRatio = static_cast<float>(player_.mana) / player_.maxMana;
    renderOrb(SCREEN_W - ORB_R - 12, orbCY, ORB_R,
              manaRatio,
              {20, 40, 200, 255},   // deep blue fill
              { 4,  8,  50, 255},   // dark blue bg
              "MANA");

    // ── Centre stats area ────────────────────────────────────────────────────
    const int midX  = SCREEN_W / 2;
    const int textY = panelY + 10;

    // Class & level
    char buf[80];
    std::snprintf(buf, sizeof(buf), "%s  LVL %d",
                  classNameStr(player_.charClass), player_.stats.level);
    int nameLen = 0; for (const char* p = buf; *p; ++p) ++nameLen;
    renderString(midX - nameLen * 3, textY, buf, {220, 180, 80, 255}, 1);

    // XP bar
    float xpRatio = static_cast<float>(player_.stats.experience)
                  / player_.stats.expToNextLevel;
    constexpr int XP_W = 300, XP_H = 6;
    int xpX = midX - XP_W / 2;
    int xpY = panelY + 28;

    SDL_SetRenderDrawColor(renderer_, 25, 20, 10, 200);
    SDL_Rect xpBg { xpX, xpY, XP_W, XP_H };
    SDL_RenderFillRect(renderer_, &xpBg);
    SDL_SetRenderDrawColor(renderer_, 200, 160, 30, 220);
    SDL_Rect xpFg { xpX, xpY, static_cast<int>(XP_W * xpRatio), XP_H };
    SDL_RenderFillRect(renderer_, &xpFg);
    // XP border
    SDL_SetRenderDrawColor(renderer_, 100, 80, 20, 180);
    SDL_RenderDrawRect(renderer_, &xpBg);
    renderString(xpX, xpY + 8, "Experience", {130, 110, 50, 200}, 1);

    // HP / Mana numbers
    std::snprintf(buf, sizeof(buf), "%d / %d", player_.health, player_.maxHealth);
    renderString(ORB_R * 2 + 20, panelY + 10, buf, {200, 80, 80, 255}, 1);

    std::snprintf(buf, sizeof(buf), "%d / %d", player_.mana, player_.maxMana);
    int manaStrLen = 0; for (const char* p = buf; *p; ++p) ++manaStrLen;
    renderString(SCREEN_W - ORB_R * 2 - 20 - manaStrLen * 6, panelY + 10,
                 buf, {80, 100, 220, 255}, 1);

    // Score
    std::snprintf(buf, sizeof(buf), "SCORE: %d", score_);
    renderString(midX - 30, panelY + 50, buf, {180, 160, 80, 255}, 1);

    // Attack cooldown bar (small, near centre bottom)
    if (player_.attackCooldown > 0.f) {
        float cdRatio = player_.attackCooldown / player_.attackCooldownMax;
        constexpr int CD_W = 80, CD_H = 5;
        int cdX = midX - CD_W / 2;
        int cdY = panelY + 70;
        SDL_SetRenderDrawColor(renderer_, 40, 30, 10, 200);
        SDL_Rect cdBg { cdX, cdY, CD_W, CD_H };
        SDL_RenderFillRect(renderer_, &cdBg);
        SDL_SetRenderDrawColor(renderer_, 255, 160, 20, 220);
        SDL_Rect cdFg { cdX, cdY, static_cast<int>(CD_W * (1.f - cdRatio)), CD_H };
        SDL_RenderFillRect(renderer_, &cdFg);
        renderString(cdX, cdY + 7, "ATK CD", {160, 120, 40, 200}, 1);
    }

    // ── Belt slot placeholders (D2-style item belt) ───────────────────────────
    constexpr int SLOT_W = 34, SLOT_H = 34, SLOT_GAP = 3;
    constexpr int BELT_SLOTS = 8;
    int beltW = BELT_SLOTS * (SLOT_W + SLOT_GAP) - SLOT_GAP;
    int beltX = midX - beltW / 2;
    int beltY = panelY + HUD_H - SLOT_H - 8;
    for (int i = 0; i < BELT_SLOTS; ++i) {
        int sx = beltX + i * (SLOT_W + SLOT_GAP);
        SDL_SetRenderDrawColor(renderer_, 30, 22, 12, 220);
        SDL_Rect slotBg { sx, beltY, SLOT_W, SLOT_H };
        SDL_RenderFillRect(renderer_, &slotBg);
        SDL_SetRenderDrawColor(renderer_, 90, 70, 35, 200);
        SDL_RenderDrawRect(renderer_, &slotBg);
    }

    // ── Controls hint (minimal, D2 doesn't clutter) ──────────────────────────
    renderString(SCREEN_W - 150, panelY + HUD_H - 14,
                 "LClick=Move RClick=Atk",
                 {80, 70, 45, 180}, 1);
}

