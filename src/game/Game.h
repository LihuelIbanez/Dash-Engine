#pragma once
#include <SDL2/SDL.h>
#include <vector>
#include <memory>
#include <string>
#include "World.h"
#include "Player.h"
#include "Enemy.h"
#include "RuntimeContext.h"
#include "SystemScheduler.h"
#include "GameplayDatabase.h"
#include "SaveGame.h"

// ─────────────────────────────────────────────────────────────────────────────
// Game – owns the SDL window/renderer and the game loop
// ─────────────────────────────────────────────────────────────────────────────
class Game {
public:
    Game();
    ~Game();

    // Set a scene file to load instead of the default hardcoded state.
    // Must be called before init().
    void setSceneFile(const std::string& path);

    bool init();
    void run();

    // ── Embedded mode (run inside editor viewport) ───────────────────────────
    bool initEmbedded(SDL_Renderer* renderer);
    void tickUpdate(float dt);
    void tickRender();                       // renders without SDL_RenderPresent
    void injectClick(int screenX, int screenY, bool leftButton);
    void injectAttack();
    bool isRunning() const { return running_; }

private:
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool          embedded_ = false;         // true when hosted by editor

    bool          running_  = false;
    World         world_;
    Player        player_;
    std::vector<std::unique_ptr<Enemy>> enemies_;

    int           score_    = 0;
    unsigned int  worldSeed_ = 12345;

    // ── Save / Load ──────────────────────────────────────────────────────────
    std::string   savesDir_;       // resolved at init
    void saveGame(const std::string& path);
    void loadGame(const std::string& path);
    SaveData captureState() const;
    void     applyState(const SaveData& data);

    // ── Runtime systems ──────────────────────────────────────────────────────
    RuntimeContext   ctx_;
    SystemScheduler  scheduler_;
    GameplayDatabase gameDb_;
    void initSystems();
    void spawnEnemiesFromData();
    bool loadSceneFile();           // apply scene JSON if sceneFile_ is set

    std::string sceneFile_;         // optional: editor scene to play

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

    // --- HUD (Diablo 2 style) ------------------------------------------------
    void renderHUD();
    void renderOrb(int cx, int cy, int radius, float ratio,
                   SDL_Color fillCol, SDL_Color bgCol, const char* label);
    void renderString(int x, int y, const char* text, SDL_Color col, int scale = 2);
    void renderChar(int px, int py, char c, SDL_Color col, int scale);
};
