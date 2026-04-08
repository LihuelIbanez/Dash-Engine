#include "Game.h"
#include "MovementSystem.h"
#include "AISystem.h"
#include "CombatSystem.h"
#include "SpawnRewardSystem.h"
#include "GameplayDatabase.h"
#include "SaveGame.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include "Font5x7.h"

// ═════════════════════════════════════════════════════════════════════════════
// initSystems — register all gameplay systems in execution order
// ═════════════════════════════════════════════════════════════════════════════
void Game::initSystems()
{
    ctx_.world   = &world_;
    ctx_.player  = &player_;
    ctx_.enemies = &enemies_;
    ctx_.score   = &score_;

    scheduler_.addSystem(std::make_unique<MovementSystem>());
    scheduler_.addSystem(std::make_unique<AISystem>());
    scheduler_.addSystem(std::make_unique<CombatSystem>());
    scheduler_.addSystem(std::make_unique<SpawnRewardSystem>());
}

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
}

void Game::setSceneFile(const std::string& path) { sceneFile_ = path; }

// ─────────────────────────────────────────────────────────────────────────────
// loadSceneFile — apply the editor scene JSON to the game world
// ─────────────────────────────────────────────────────────────────────────────
bool Game::loadSceneFile()
{
    std::ifstream f(sceneFile_);
    if (!f.is_open()) return false;

    nlohmann::json j;
    try { f >> j; } catch (...) { return false; }
    if (!j.is_object()) return false;

    // ── World seed + regenerate ──────────────────────────────────────────────
    unsigned int seed = j.value("worldSeed", 12345u);
    worldSeed_ = seed;
    std::srand(seed);
    world_.generate(seed);

    // ── Tile overrides ───────────────────────────────────────────────────────
    if (j.contains("tileOverrides") && j["tileOverrides"].is_array()) {
        for (auto& t : j["tileOverrides"]) {
            int tx = t.value("x", -1);
            int ty = t.value("y", -1);
            if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) continue;
            world_.grid[ty][tx].type     = static_cast<TileType>(t.value("type", 0));
            world_.grid[ty][tx].walkable = t.value("walkable", true);
        }
    }

    // ── Entities ─────────────────────────────────────────────────────────────
    if (j.contains("entities") && j["entities"].is_array()) {
        for (auto& ej : j["entities"]) {
            std::string type = ej.value("type", "Enemy");
            std::string name = ej.value("name", "Unknown");
            float ex = ej.value("x", 0.f);
            float ey = ej.value("y", 0.f);

            if (type == "Player") {
                // Reposition player
                player_.x = ex;
                player_.y = ey;

                // Apply class from scene
                std::string cls = ej.value("class", "Warrior");
                // Convert to lowercase for GameplayDatabase lookup
                std::string clsLower = cls;
                for (auto& c : clsLower) c = static_cast<char>(std::tolower(c));

                if (auto* pcd = gameDb_.findPlayerClass(clsLower)) {
                    player_.stats.attack      = pcd->attack;
                    player_.stats.defense     = pcd->defense;
                    player_.stats.magicAttack = pcd->magicAttack;
                    player_.stats.speed       = pcd->speed;
                    player_.stats.critChance  = pcd->critChance;
                    player_.maxHealth         = pcd->maxHp;
                    player_.health            = pcd->maxHp;
                    player_.maxMana           = pcd->maxMana;
                    player_.mana              = pcd->maxMana;
                    player_.attackCooldownMax = pcd->attackCooldown;
                }
            } else {
                // Enemy: look up in GameplayDatabase by lowercase name
                std::string nameLower = name;
                for (auto& c : nameLower) c = static_cast<char>(std::tolower(c));

                if (auto* ed = gameDb_.findEnemy(nameLower)) {
                    enemies_.push_back(std::make_unique<Enemy>(ex, ey, *ed));
                } else {
                    enemies_.push_back(std::make_unique<Enemy>(ex, ey, name));
                }
            }
        }
    }

    return true;
}

void Game::spawnEnemiesFromData()
{
    const float cx = static_cast<float>(WORLD_W) / 2.f;
    const float cy = static_cast<float>(WORLD_H) / 2.f;

    // Spawn layout: name → offset pairs
    struct SpawnInfo { const char* type; float ox; float oy; };
    SpawnInfo spawns[] = {
        { "skeleton",  4.f,  3.f },
        { "zombie",   -5.f,  2.f },
        { "skeleton",  2.f, -5.f },
        { "fallen",   -3.f, -4.f },
        { "zombie",    6.f, -2.f },
    };

    for (auto& s : spawns) {
        if (auto* data = gameDb_.findEnemy(s.type)) {
            enemies_.push_back(
                std::make_unique<Enemy>(cx + s.ox, cy + s.oy, *data));
        } else {
            // Fallback: create with default constructor using type as name
            enemies_.push_back(
                std::make_unique<Enemy>(cx + s.ox, cy + s.oy, std::string(s.type)));
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Save / Load
// ═════════════════════════════════════════════════════════════════════════════
SaveData Game::captureState() const
{
    SaveData d;
    d.worldSeed = worldSeed_;
    d.score     = score_;

    // Player
    auto& p      = d.player;
    p.x            = player_.x;
    p.y            = player_.y;
    p.health       = player_.health;
    p.maxHealth    = player_.maxHealth;
    p.mana         = player_.mana;
    p.maxMana      = player_.maxMana;
    p.attack       = player_.stats.attack;
    p.defense      = player_.stats.defense;
    p.magicAttack  = player_.stats.magicAttack;
    p.speed        = player_.stats.speed;
    p.critChance   = player_.stats.critChance;
    p.level        = player_.stats.level;
    p.experience   = player_.stats.experience;
    p.expToNext    = player_.stats.expToNextLevel;
    p.atkCooldownMax = player_.attackCooldownMax;
    p.charClass    = classNameStr(player_.charClass);

    // Enemies (only alive ones)
    for (auto& ep : enemies_) {
        if (!ep->isAlive()) continue;
        SaveEnemyData se;
        se.x               = ep->x;
        se.y               = ep->y;
        se.health          = ep->health;
        se.maxHealth       = ep->maxHealth;
        se.alive           = ep->alive;
        se.name            = ep->name;
        se.attack          = ep->stats.attack;
        se.defense         = ep->stats.defense;
        se.magicAttack     = ep->stats.magicAttack;
        se.speed           = ep->stats.speed;
        se.critChance      = ep->stats.critChance;
        se.detectionRadius = ep->detectionRadius;
        se.attackRadius    = ep->attackRadius;
        se.expReward       = ep->expReward;
        se.atkCooldownMax  = ep->attackCooldownMax;
        d.enemies.push_back(se);
    }

    return d;
}

void Game::applyState(const SaveData& data)
{
    worldSeed_ = data.worldSeed;
    score_     = data.score;
    world_.generate(worldSeed_);

    // Player
    auto& p = data.player;
    player_.x            = p.x;
    player_.y            = p.y;
    player_.health       = p.health;
    player_.maxHealth    = p.maxHealth;
    player_.mana         = p.mana;
    player_.maxMana      = p.maxMana;
    player_.stats.attack      = p.attack;
    player_.stats.defense     = p.defense;
    player_.stats.magicAttack = p.magicAttack;
    player_.stats.speed       = p.speed;
    player_.stats.critChance  = p.critChance;
    player_.stats.level       = p.level;
    player_.stats.experience  = p.experience;
    player_.stats.expToNextLevel = p.expToNext;
    player_.attackCooldownMax = p.atkCooldownMax;
    player_.hasTarget = false;

    // Enemies — recreate from save data
    enemies_.clear();
    for (auto& se : data.enemies) {
        auto enemy = std::make_unique<Enemy>(se.x, se.y, se.name);
        enemy->health       = se.health;
        enemy->maxHealth    = se.maxHealth;
        enemy->alive        = se.alive;
        enemy->stats.attack      = se.attack;
        enemy->stats.defense     = se.defense;
        enemy->stats.magicAttack = se.magicAttack;
        enemy->stats.speed       = se.speed;
        enemy->stats.critChance  = se.critChance;
        enemy->detectionRadius   = se.detectionRadius;
        enemy->attackRadius      = se.attackRadius;
        enemy->expReward         = se.expReward;
        enemy->attackCooldownMax = se.atkCooldownMax;
        enemies_.push_back(std::move(enemy));
    }
}

void Game::saveGame(const std::string& path)
{
    SaveData data = captureState();
    if (SaveGame::save(data, path))
        std::printf("[Game] Saved to %s\n", path.c_str());
    else
        std::printf("[Game] ERROR: Could not save to %s\n", path.c_str());
}

void Game::loadGame(const std::string& path)
{
    SaveData data;
    if (SaveGame::load(path, data)) {
        applyState(data);
        std::printf("[Game] Loaded from %s (v%d)\n", path.c_str(), data.saveVersion);
    } else {
        std::printf("[Game] ERROR: Could not load %s\n", path.c_str());
    }
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

    // ── Saves directory ──────────────────────────────────────────────────────
    savesDir_ = "saves";
    std::filesystem::create_directories(savesDir_);

    // ── Load world and scene data ────────────────────────────────────────────
    worldSeed_ = 12345;
    std::srand(worldSeed_);
    world_.generate(worldSeed_);
    gameDb_.load("assets");

    if (!sceneFile_.empty() && loadSceneFile()) {
        std::printf("[Game] Loaded scene: %s\n", sceneFile_.c_str());
    } else {
        if (auto* cls = gameDb_.findPlayerClass("warrior")) {
            player_.stats.attack      = cls->attack;
            player_.stats.defense     = cls->defense;
            player_.stats.magicAttack = cls->magicAttack;
            player_.stats.speed       = cls->speed;
            player_.stats.critChance  = cls->critChance;
            player_.maxHealth         = cls->maxHp;
            player_.health            = cls->maxHp;
            player_.maxMana           = cls->maxMana;
            player_.mana              = cls->maxMana;
            player_.attackCooldownMax = cls->attackCooldown;
        }
        spawnEnemiesFromData();
    }

    initSystems();
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

        // F5 = Quick save, F9 = Quick load
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F5) {
            saveGame(savesDir_ + "/quicksave.json");
        }
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F9) {
            loadGame(savesDir_ + "/quicksave.json");
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
// Update — delegates to SystemScheduler
// ─────────────────────────────────────────────────────────────────────────────
void Game::update(float dt)
{
    ctx_.dt      = dt;
    ctx_.running = running_;

    scheduler_.updateAll(ctx_);

    running_ = ctx_.running;
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

