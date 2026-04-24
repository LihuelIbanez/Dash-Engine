#pragma once
#include <SDL2/SDL.h>
#include "SceneData.h"
#include "World.h"
#include "Player.h"
#include "Enemy.h"
#include "RuntimeContext.h"
#include "SystemScheduler.h"
#include "EventDispatcher.h"
#include <vector>
#include <memory>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// EntityViewportPanel — 3D perspective viewport for testing entity behaviour
// ─────────────────────────────────────────────────────────────────────────────
class EntityViewportPanel {
public:
    void init(SDL_Renderer* renderer, const std::string& assetsRoot);
    void draw(const SceneData& editorScene, SDL_Renderer* renderer);
    void shutdown();

    bool isOpen = false;

private:
    // ── Render target ────────────────────────────────────────────────────────
    SDL_Texture* texture_ = nullptr;
    int texW_ = 800;
    int texH_ = 600;

    // ── Simulation ───────────────────────────────────────────────────────────
    enum class SimState { Stopped, Playing, Paused };
    SimState simState_ = SimState::Stopped;

    World                                simWorld_;
    Player                               simPlayer_{128.f, 128.f};
    std::vector<std::unique_ptr<Enemy>>  simEnemies_;
    RuntimeContext                       simCtx_;
    SystemScheduler                      simScheduler_;
    EventDispatcher                      simDispatcher_;
    int                                  simScore_ = 0;

    void startSimulation(const SceneData& scene);
    void stopSimulation();
    void tickSimulation(float dt);

    // ── Camera (orbit) ───────────────────────────────────────────────────────
    float camYaw_      = 45.0f;   // degrees
    float camPitch_    = 30.0f;   // degrees
    float camDistance_  = 80.0f;
    float camFocusX_   = 0.f;
    float camFocusY_   = 0.f;     // up axis
    float camFocusZ_   = 0.f;
    float camFOV_      = 60.0f;

    // ── Mouse interaction ────────────────────────────────────────────────────
    int selectedEntity_ = -1;  // -1 = player, 0+ = enemy index
    bool needsCenterOnEntities_ = true;  // auto-center on first draw

    // ── Rendering ────────────────────────────────────────────────────────────
    void renderToTexture(SDL_Renderer* renderer, const SceneData& scene);
    void renderGroundGrid(SDL_Renderer* renderer);
    void renderAxisGizmo(SDL_Renderer* renderer);
    void renderEntities(SDL_Renderer* renderer, const SceneData& scene);

    // ── Perspective math ─────────────────────────────────────────────────────
    struct ScreenPoint { float x, y, depth; bool visible; };

    float viewProj_[16] = {};
    float camEyeX_ = 0.f, camEyeY_ = 0.f, camEyeZ_ = 0.f;

    void buildViewProjection();
    ScreenPoint project(float wx, float wy, float wz) const;

    std::string assetsRoot_;
};
