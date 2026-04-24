#include "EntityViewportPanel.h"
#include "TextureCache.h"
#include "AISystem.h"
#include "MovementSystem.h"
#include "CombatSystem.h"
#include "imgui.h"
#include <cmath>
#include <algorithm>

// ═════════════════════════════════════════════════════════════════════════════
// Perspective math helpers
// ═════════════════════════════════════════════════════════════════════════════
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

void mat4Identity(float m[16])
{
    for (int i = 0; i < 16; ++i) m[i] = 0.f;
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

void mat4Mul(float out[16], const float a[16], const float b[16])
{
    float tmp[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k)
                s += a[r * 4 + k] * b[k * 4 + c];
            tmp[r * 4 + c] = s;
        }
    for (int i = 0; i < 16; ++i) out[i] = tmp[i];
}

void mat4LookAt(float m[16],
                float eyeX, float eyeY, float eyeZ,
                float tgtX, float tgtY, float tgtZ,
                float upX,  float upY,  float upZ)
{
    float fx = tgtX - eyeX, fy = tgtY - eyeY, fz = tgtZ - eyeZ;
    float fLen = std::sqrt(fx*fx + fy*fy + fz*fz);
    if (fLen < 1e-8f) { mat4Identity(m); return; }
    fx /= fLen; fy /= fLen; fz /= fLen;

    float rx = fy * upZ - fz * upY;
    float ry = fz * upX - fx * upZ;
    float rz = fx * upY - fy * upX;
    float rLen = std::sqrt(rx*rx + ry*ry + rz*rz);
    if (rLen < 1e-8f) { mat4Identity(m); return; }
    rx /= rLen; ry /= rLen; rz /= rLen;

    float ux = ry * fz - rz * fy;
    float uy = rz * fx - rx * fz;
    float uz = rx * fy - ry * fx;

    m[ 0] = rx;  m[ 1] = ux;  m[ 2] = -fx; m[ 3] = 0.f;
    m[ 4] = ry;  m[ 5] = uy;  m[ 6] = -fy; m[ 7] = 0.f;
    m[ 8] = rz;  m[ 9] = uz;  m[10] = -fz; m[11] = 0.f;
    m[12] = -(rx*eyeX + ry*eyeY + rz*eyeZ);
    m[13] = -(ux*eyeX + uy*eyeY + uz*eyeZ);
    m[14] =  (fx*eyeX + fy*eyeY + fz*eyeZ);
    m[15] = 1.f;
}

void mat4Perspective(float m[16], float fovYDeg, float aspect, float zNear, float zFar)
{
    float f = 1.f / std::tan(fovYDeg * kDeg2Rad * 0.5f);
    for (int i = 0; i < 16; ++i) m[i] = 0.f;
    m[ 0] = f / aspect;
    m[ 5] = f;
    m[10] = (zFar + zNear) / (zNear - zFar);
    m[11] = -1.f;
    m[14] = 2.f * zFar * zNear / (zNear - zFar);
}

// Draw a thick line (3px wide) using SDL2
void drawThickLine(SDL_Renderer* r, float x0, float y0, float x1, float y1, int thickness)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx*dx + dy*dy);
    if (len < 0.5f) return;
    float nx = -dy / len, ny = dx / len;
    for (int t = -thickness/2; t <= thickness/2; ++t) {
        float off = static_cast<float>(t);
        SDL_RenderDrawLineF(r, x0 + nx*off, y0 + ny*off, x1 + nx*off, y1 + ny*off);
    }
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════════
// Init / Shutdown
// ═════════════════════════════════════════════════════════════════════════════
void EntityViewportPanel::init(SDL_Renderer* renderer, const std::string& assetsRoot)
{
    assetsRoot_ = assetsRoot;
    texture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                 SDL_TEXTUREACCESS_TARGET, texW_, texH_);
    if (texture_)
        SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
}

void EntityViewportPanel::shutdown()
{
    stopSimulation();
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Simulation lifecycle
// ═════════════════════════════════════════════════════════════════════════════
void EntityViewportPanel::startSimulation(const SceneData& scene)
{
    stopSimulation();

    simWorld_.generate(1u);
    for (int y = 0; y < WORLD_H; ++y)
        for (int x = 0; x < WORLD_W; ++x) {
            simWorld_.grid[y][x].type = TileType::Grass;
            simWorld_.grid[y][x].walkable = true;
        }

    bool playerFound = false;
    for (const auto& ent : scene.entities) {
        if (ent.type == EntityData::Type::Player) {
            simPlayer_ = Player(ent.x, ent.y);
            simPlayer_.name = ent.name;
            simPlayer_.setWorld(&simWorld_);
            playerFound = true;
            for (const auto& cv : ent.components) {
                if (auto* hc = std::get_if<HealthComponent>(&cv)) {
                    simPlayer_.health = hc->health;
                    simPlayer_.maxHealth = hc->maxHealth;
                }
                if (auto* sc = std::get_if<StatsComponent>(&cv)) {
                    simPlayer_.stats.attack = sc->attack;
                    simPlayer_.stats.defense = sc->defense;
                    simPlayer_.stats.speed = static_cast<float>(sc->speed);
                }
            }
        } else {
            auto enemy = std::make_unique<Enemy>(ent.x, ent.y, ent.name);
            for (const auto& cv : ent.components) {
                if (auto* hc = std::get_if<HealthComponent>(&cv)) {
                    enemy->health = hc->health;
                    enemy->maxHealth = hc->maxHealth;
                }
                if (auto* sc = std::get_if<StatsComponent>(&cv)) {
                    enemy->stats.attack = sc->attack;
                    enemy->stats.defense = sc->defense;
                    enemy->stats.speed = static_cast<float>(sc->speed);
                }
                if (auto* ac = std::get_if<AIComponent>(&cv)) {
                    enemy->detectionRadius = ac->detectionRange;
                }
            }
            simEnemies_.push_back(std::move(enemy));
        }
    }

    if (!playerFound) {
        simPlayer_ = Player(128.f, 128.f);
        simPlayer_.setWorld(&simWorld_);
    }

    camFocusX_ = simPlayer_.x * TILE_SCALE;
    camFocusY_ = 0.f;
    camFocusZ_ = simPlayer_.y * TILE_SCALE;

    simScore_ = 0;
    simDispatcher_.clear();
    simCtx_.dt = 0.f;
    simCtx_.running = true;
    simCtx_.world = &simWorld_;
    simCtx_.player = &simPlayer_;
    simCtx_.enemies = &simEnemies_;
    simCtx_.score = &simScore_;
    simCtx_.events = &simDispatcher_;

    simScheduler_ = SystemScheduler{};
    simScheduler_.addSystem(std::make_unique<MovementSystem>());
    simScheduler_.addSystem(std::make_unique<AISystem>());
    simScheduler_.addSystem(std::make_unique<CombatSystem>());

    simState_ = SimState::Playing;
}

void EntityViewportPanel::stopSimulation()
{
    simEnemies_.clear();
    simDispatcher_.clear();
    simScheduler_ = SystemScheduler{};
    simState_ = SimState::Stopped;
}

void EntityViewportPanel::tickSimulation(float dt)
{
    dt = std::min(dt, 0.05f);
    simCtx_.dt = dt;
    simCtx_.running = true;
    simScheduler_.updateAll(simCtx_);
    simDispatcher_.flush();
}

// ═════════════════════════════════════════════════════════════════════════════
// Camera
// ═════════════════════════════════════════════════════════════════════════════
void EntityViewportPanel::buildViewProjection()
{
    float yawRad   = camYaw_   * kDeg2Rad;
    float pitchRad = camPitch_ * kDeg2Rad;

    float cosPitch = std::cos(pitchRad);
    camEyeX_ = camFocusX_ + camDistance_ * cosPitch * std::sin(yawRad);
    camEyeY_ = camFocusY_ + camDistance_ * std::sin(pitchRad);
    camEyeZ_ = camFocusZ_ + camDistance_ * cosPitch * std::cos(yawRad);

    float view[16], proj[16];
    mat4LookAt(view,
               camEyeX_, camEyeY_, camEyeZ_,
               camFocusX_, camFocusY_, camFocusZ_,
               0.f, 1.f, 0.f);
    float aspect = static_cast<float>(texW_) / static_cast<float>(texH_);
    mat4Perspective(proj, camFOV_, aspect, 0.1f, 1000.f);
    mat4Mul(viewProj_, proj, view);
}

EntityViewportPanel::ScreenPoint EntityViewportPanel::project(float wx, float wy, float wz) const
{
    float x = viewProj_[0]*wx + viewProj_[4]*wy + viewProj_[ 8]*wz + viewProj_[12];
    float y = viewProj_[1]*wx + viewProj_[5]*wy + viewProj_[ 9]*wz + viewProj_[13];
    float w = viewProj_[3]*wx + viewProj_[7]*wy + viewProj_[11]*wz + viewProj_[15];

    if (w <= 0.001f)
        return { 0.f, 0.f, 0.f, false };

    float ndcX = x / w;
    float ndcY = y / w;

    float sx = (ndcX * 0.5f + 0.5f) * texW_;
    float sy = (1.f - (ndcY * 0.5f + 0.5f)) * texH_;

    return { sx, sy, w, true };
}

// ═════════════════════════════════════════════════════════════════════════════
// Rendering
// ═════════════════════════════════════════════════════════════════════════════
void EntityViewportPanel::renderToTexture(SDL_Renderer* renderer, const SceneData& scene)
{
    SDL_SetRenderTarget(renderer, texture_);
    // Dark background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer, 35, 35, 42, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    buildViewProjection();
    renderGroundGrid(renderer);
    renderEntities(renderer, scene);
    renderAxisGizmo(renderer);

    SDL_SetRenderTarget(renderer, nullptr);
}

void EntityViewportPanel::renderGroundGrid(SDL_Renderer* renderer)
{
    constexpr int gridExtent = 50;
    constexpr float spacing = TILE_SCALE;
    constexpr float majorEvery = 10.f;

    float cx = std::floor(camFocusX_ / spacing) * spacing;
    float cz = std::floor(camFocusZ_ / spacing) * spacing;

    for (int i = -gridExtent; i <= gridExtent; ++i) {
        float offset = i * spacing;
        float worldPos = (cx + offset) / spacing;
        bool isMajor = (std::abs(std::fmod(worldPos, majorEvery)) < 0.5f);

        if (isMajor)
            SDL_SetRenderDrawColor(renderer, 150, 150, 165, 255);
        else
            SDL_SetRenderDrawColor(renderer, 90, 90, 105, 180);

        // Lines along Z
        auto p0 = project(cx + offset, 0.f, cz - gridExtent * spacing);
        auto p1 = project(cx + offset, 0.f, cz + gridExtent * spacing);
        if (p0.visible && p1.visible)
            SDL_RenderDrawLineF(renderer, p0.x, p0.y, p1.x, p1.y);

        // Lines along X
        float worldPosZ = (cz + offset) / spacing;
        bool isMajorZ = (std::abs(std::fmod(worldPosZ, majorEvery)) < 0.5f);
        if (isMajorZ)
            SDL_SetRenderDrawColor(renderer, 150, 150, 165, 255);
        else
            SDL_SetRenderDrawColor(renderer, 90, 90, 105, 180);

        p0 = project(cx - gridExtent * spacing, 0.f, cz + offset);
        p1 = project(cx + gridExtent * spacing, 0.f, cz + offset);
        if (p0.visible && p1.visible)
            SDL_RenderDrawLineF(renderer, p0.x, p0.y, p1.x, p1.y);
    }

    // ── World origin axes on the ground plane ────────────────────────────────
    constexpr float axisLen = 20.f * TILE_SCALE;

    // X axis = Red (thick)
    auto o = project(0.f, 0.f, 0.f);
    auto ax = project(axisLen, 0.f, 0.f);
    if (o.visible && ax.visible) {
        SDL_SetRenderDrawColor(renderer, 220, 50, 50, 255);
        drawThickLine(renderer, o.x, o.y, ax.x, ax.y, 2);
    }

    // Z axis = Blue (thick)
    auto az = project(0.f, 0.f, axisLen);
    if (o.visible && az.visible) {
        SDL_SetRenderDrawColor(renderer, 50, 80, 220, 255);
        drawThickLine(renderer, o.x, o.y, az.x, az.y, 2);
    }

    // Y axis = Green (vertical, thick)
    auto ay = project(0.f, axisLen * 0.5f, 0.f);
    if (o.visible && ay.visible) {
        SDL_SetRenderDrawColor(renderer, 50, 200, 50, 255);
        drawThickLine(renderer, o.x, o.y, ay.x, ay.y, 2);
    }
}

void EntityViewportPanel::renderAxisGizmo(SDL_Renderer* renderer)
{
    // Corner axis gizmo (screen-space, top-left)
    constexpr float gizmoSize = 50.f;
    constexpr float gizmoMargin = 15.f;
    float gcx = gizmoMargin + gizmoSize;
    float gcy = gizmoMargin + gizmoSize;

    // Build a view-only rotation (no translation, no projection perspective)
    float yawRad   = camYaw_   * kDeg2Rad;
    float pitchRad = camPitch_ * kDeg2Rad;

    float cosPitch = std::cos(pitchRad);
    float sinPitch = std::sin(pitchRad);
    float cosYaw   = std::cos(yawRad);
    float sinYaw   = std::sin(yawRad);

    // Camera right, up, forward vectors
    float rx = cosYaw,  ry = 0.f,      rz = -sinYaw;
    float ux = -sinYaw * sinPitch, uy = cosPitch, uz = -cosYaw * sinPitch;

    // Project a 3D direction to 2D gizmo space (only rotation, no depth)
    auto gizmoProject = [&](float dx, float dy, float dz) -> std::pair<float, float> {
        float sx = rx * dx + ry * dy + rz * dz;
        float sy = ux * dx + uy * dy + uz * dz;
        return { gcx + sx * gizmoSize, gcy - sy * gizmoSize };
    };

    // Background circle
    SDL_SetRenderDrawColor(renderer, 20, 20, 25, 180);
    for (int dy = -static_cast<int>(gizmoSize) - 5; dy <= static_cast<int>(gizmoSize) + 5; ++dy) {
        int hw = static_cast<int>(std::sqrt(std::max(0.f, (gizmoSize + 5) * (gizmoSize + 5) - dy * dy)));
        SDL_RenderDrawLine(renderer,
            static_cast<int>(gcx) - hw, static_cast<int>(gcy) + dy,
            static_cast<int>(gcx) + hw, static_cast<int>(gcy) + dy);
    }

    // X axis = Red
    auto [xx, xy] = gizmoProject(1.f, 0.f, 0.f);
    SDL_SetRenderDrawColor(renderer, 230, 60, 60, 255);
    drawThickLine(renderer, gcx, gcy, xx, xy, 2);

    // Y axis = Green
    auto [yx, yy] = gizmoProject(0.f, 1.f, 0.f);
    SDL_SetRenderDrawColor(renderer, 60, 210, 60, 255);
    drawThickLine(renderer, gcx, gcy, yx, yy, 2);

    // Z axis = Blue
    auto [zx, zy] = gizmoProject(0.f, 0.f, 1.f);
    SDL_SetRenderDrawColor(renderer, 60, 100, 230, 255);
    drawThickLine(renderer, gcx, gcy, zx, zy, 2);

    // Axis labels
    // Small filled circles at axis tips
    auto drawDot = [&](float cx_, float cy_, Uint8 r, Uint8 g, Uint8 b) {
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        for (int dy = -4; dy <= 4; ++dy) {
            int hw = static_cast<int>(std::sqrt(std::max(0.f, 16.f - dy * dy)));
            SDL_RenderDrawLine(renderer,
                static_cast<int>(cx_) - hw, static_cast<int>(cy_) + dy,
                static_cast<int>(cx_) + hw, static_cast<int>(cy_) + dy);
        }
    };
    drawDot(xx, xy, 230, 60, 60);    // X tip
    drawDot(yx, yy, 60, 210, 60);    // Y tip
    drawDot(zx, zy, 60, 100, 230);   // Z tip
}

void EntityViewportPanel::renderEntities(SDL_Renderer* renderer, const SceneData& scene)
{
    struct EntityDraw {
        float screenX, screenY, depth;
        bool isPlayer;
        bool alive;
        std::string spriteName;
        float healthRatio;
        int entityIndex;  // -1 = player, 0+ = enemy
    };

    std::vector<EntityDraw> drawList;

    if (simState_ != SimState::Stopped) {
        // Live simulation entities
        float wx = simPlayer_.x * TILE_SCALE;
        float wz = simPlayer_.y * TILE_SCALE;
        auto sp = project(wx, 0.5f, wz);
        if (sp.visible) {
            float hr = (simPlayer_.maxHealth > 0)
                ? static_cast<float>(simPlayer_.health) / simPlayer_.maxHealth : 1.f;
            drawList.push_back({ sp.x, sp.y, sp.depth, true, simPlayer_.isAlive(),
                                 "default", hr, -1 });
        }

        for (size_t i = 0; i < simEnemies_.size(); ++i) {
            const auto& e = simEnemies_[i];
            float ex = e->x * TILE_SCALE;
            float ez = e->y * TILE_SCALE;
            auto ep = project(ex, 0.5f, ez);
            if (ep.visible) {
                float hr = (e->maxHealth > 0)
                    ? static_cast<float>(e->health) / e->maxHealth : 1.f;
                drawList.push_back({ ep.x, ep.y, ep.depth, false, e->isAlive(),
                                     e->name, hr, static_cast<int>(i) });
            }
        }
    } else {
        // Preview: show editor scene entities (static preview)
        for (size_t i = 0; i < scene.entities.size(); ++i) {
            const auto& ent = scene.entities[i];
            bool isPlayer = (ent.type == EntityData::Type::Player);
            float wx = ent.x * TILE_SCALE;
            float wz = ent.y * TILE_SCALE;
            auto sp = project(wx, 0.5f, wz);
            if (sp.visible) {
                // Get sprite name from render component
                std::string sprite = "default";
                for (const auto& cv : ent.components) {
                    if (auto* rc = std::get_if<RenderComponent>(&cv))
                        sprite = rc->sprite;
                }
                drawList.push_back({ sp.x, sp.y, sp.depth, isPlayer, true,
                                     sprite, 1.f, isPlayer ? -1 : static_cast<int>(i) });
            }
        }
    }

    std::sort(drawList.begin(), drawList.end(),
              [](const EntityDraw& a, const EntityDraw& b) {
                  return a.depth > b.depth;
              });

    for (const auto& ed : drawList) {
        float baseSize = 600.f / ed.depth;
        baseSize = std::max(6.f, std::min(120.f, baseSize));
        int halfSize = static_cast<int>(baseSize * 0.5f);

        std::string spritePath = assetsRoot_ + "/sprites/" + ed.spriteName + ".png";
        SDL_Texture* spriteTex = TextureCache::instance().load(renderer, spritePath);

        if (spriteTex) {
            int tw, th;
            SDL_QueryTexture(spriteTex, nullptr, nullptr, &tw, &th);
            float aspect = static_cast<float>(tw) / std::max(1, th);
            int drawW = static_cast<int>(baseSize * aspect);
            int drawH = static_cast<int>(baseSize);
            SDL_Rect dst = {
                static_cast<int>(ed.screenX) - drawW / 2,
                static_cast<int>(ed.screenY) - drawH,
                drawW, drawH
            };
            SDL_SetTextureAlphaMod(spriteTex, ed.alive ? 255 : 80);
            SDL_RenderCopy(renderer, spriteTex, nullptr, &dst);
        } else {
            Uint8 r, g, b;
            if (ed.isPlayer) { r = 60; g = 120; b = 220; }
            else             { r = 220; g = 60; b = 60; }
            if (!ed.alive) { r /= 3; g /= 3; b /= 3; }

            SDL_SetRenderDrawColor(renderer, r, g, b, 255);
            int cx = static_cast<int>(ed.screenX);
            int cy = static_cast<int>(ed.screenY) - halfSize;
            for (int dy = -halfSize; dy <= halfSize; ++dy) {
                int hw = halfSize - std::abs(dy);
                SDL_RenderDrawLine(renderer, cx - hw, cy + dy, cx + hw, cy + dy);
            }

            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 180);
            SDL_RenderDrawLine(renderer, cx, cy - halfSize, cx + halfSize, cy);
            SDL_RenderDrawLine(renderer, cx + halfSize, cy, cx, cy + halfSize);
            SDL_RenderDrawLine(renderer, cx, cy + halfSize, cx - halfSize, cy);
            SDL_RenderDrawLine(renderer, cx - halfSize, cy, cx, cy - halfSize);
        }

        if (ed.alive && ed.healthRatio < 1.f) {
            int barW = static_cast<int>(baseSize);
            int barH = 4;
            int barX = static_cast<int>(ed.screenX) - barW / 2;
            int barY = static_cast<int>(ed.screenY) - static_cast<int>(baseSize) - 8;

            SDL_Rect bgRect = { barX, barY, barW, barH };
            SDL_SetRenderDrawColor(renderer, 40, 40, 40, 200);
            SDL_RenderFillRect(renderer, &bgRect);

            SDL_Rect fillRect = { barX, barY,
                                  static_cast<int>(barW * ed.healthRatio), barH };
            SDL_SetRenderDrawColor(renderer, 60, 200, 60, 255);
            SDL_RenderFillRect(renderer, &fillRect);
        }

        if (ed.alive) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 120);
            int dotX = static_cast<int>(ed.screenX);
            int dotY = static_cast<int>(ed.screenY);
            SDL_Rect dot = { dotX - 1, dotY - 1, 3, 3 };
            SDL_RenderFillRect(renderer, &dot);
        }

        // Selection ring
        if (ed.entityIndex == selectedEntity_) {
            SDL_SetRenderDrawColor(renderer, 255, 220, 50, 255);
            int cx = static_cast<int>(ed.screenX);
            int cy = static_cast<int>(ed.screenY);
            int ringR = static_cast<int>(baseSize * 0.6f);
            // Draw circle outline
            for (int a = 0; a < 32; ++a) {
                float a0 = a * kPi * 2.f / 32.f;
                float a1 = (a + 1) * kPi * 2.f / 32.f;
                SDL_RenderDrawLineF(renderer,
                    cx + std::cos(a0) * ringR, cy + std::sin(a0) * ringR,
                    cx + std::cos(a1) * ringR, cy + std::sin(a1) * ringR);
            }
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// ImGui draw
// ═════════════════════════════════════════════════════════════════════════════
void EntityViewportPanel::draw(const SceneData& editorScene, SDL_Renderer* renderer)
{
    ImGui::Begin("Entity Viewport", &isOpen);

    // ── Auto-center on entities when first opened ────────────────────────────
    if (needsCenterOnEntities_ && !editorScene.entities.empty()) {
        float avgX = 0.f, avgY = 0.f;
        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
        for (const auto& ent : editorScene.entities) {
            avgX += ent.x;
            avgY += ent.y;
            minX = std::min(minX, ent.x);
            maxX = std::max(maxX, ent.x);
            minY = std::min(minY, ent.y);
            maxY = std::max(maxY, ent.y);
        }
        avgX /= editorScene.entities.size();
        avgY /= editorScene.entities.size();
        camFocusX_ = avgX * TILE_SCALE;
        camFocusZ_ = avgY * TILE_SCALE;
        camFocusY_ = 0.f;
        // Distance based on entity spread — enough to see all entities
        float spreadX = (maxX - minX) * TILE_SCALE;
        float spreadZ = (maxY - minY) * TILE_SCALE;
        float spread = std::max(spreadX, spreadZ);
        camDistance_ = std::max(60.f, spread * 1.2f);
        needsCenterOnEntities_ = false;
    }

    // ── Toolbar ──────────────────────────────────────────────────────────────
    bool canPlay  = (simState_ == SimState::Stopped || simState_ == SimState::Paused);
    bool canPause = (simState_ == SimState::Playing);
    bool canStop  = (simState_ != SimState::Stopped);

    if (!canPlay) ImGui::BeginDisabled();
    if (ImGui::Button("Play")) {
        if (simState_ == SimState::Paused)
            simState_ = SimState::Playing;
        else
            startSimulation(editorScene);
    }
    if (!canPlay) ImGui::EndDisabled();

    ImGui::SameLine();
    if (!canPause) ImGui::BeginDisabled();
    if (ImGui::Button("Pause"))
        simState_ = SimState::Paused;
    if (!canPause) ImGui::EndDisabled();

    ImGui::SameLine();
    if (!canStop) ImGui::BeginDisabled();
    if (ImGui::Button("Stop"))
        stopSimulation();
    if (!canStop) ImGui::EndDisabled();

    ImGui::SameLine();
    if (!canStop) ImGui::BeginDisabled();
    if (ImGui::Button("Reset"))
        startSimulation(editorScene);
    if (!canStop) ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Center")) {
        needsCenterOnEntities_ = true;
    }
    ImGui::SameLine();
    switch (simState_) {
    case SimState::Stopped: ImGui::TextColored({0.5f,0.5f,0.5f,1.f}, "Stopped"); break;
    case SimState::Playing: ImGui::TextColored({0.3f,0.9f,0.3f,1.f}, "Playing"); break;
    case SimState::Paused:  ImGui::TextColored({0.9f,0.9f,0.3f,1.f}, "Paused");  break;
    }

    if (simState_ != SimState::Stopped) {
        ImGui::SameLine();
        ImGui::Text("| Enemies: %d alive", (int)std::count_if(
            simEnemies_.begin(), simEnemies_.end(),
            [](const auto& e) { return e->isAlive(); }));
        ImGui::SameLine();
        ImGui::Text("| Player HP: %d/%d", simPlayer_.health, simPlayer_.maxHealth);
    }

    ImGui::Separator();

    // Camera info
    ImGui::TextColored({0.6f,0.6f,0.6f,1.f},
        "Cam: focus(%.0f,%.0f,%.0f) dist=%.0f | Entities: %d",
        camFocusX_, camFocusY_, camFocusZ_, camDistance_,
        static_cast<int>(editorScene.entities.size()));
    ImGui::Separator();
    if (simState_ == SimState::Playing)
        tickSimulation(ImGui::GetIO().DeltaTime);

    // ── Render viewport ──────────────────────────────────────────────────────
    renderToTexture(renderer, editorScene);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1.f) avail.x = 1.f;
    if (avail.y < 1.f) avail.y = 1.f;

    // ── Layout: Entity list (left) + Viewport (right) ────────────────────────
    float listWidth = 200.f;
    if (avail.x < 400.f) listWidth = avail.x * 0.3f;
    float vpWidth = avail.x - listWidth - 8.f;

    // ── Entity list panel ────────────────────────────────────────────────────
    ImGui::BeginChild("##EntityList", { listWidth, avail.y }, true);
    ImGui::Text("Entities");
    ImGui::Separator();

    if (simState_ == SimState::Stopped) {
        // Show entities from the editor scene
        ImGui::TextColored({0.5f,0.5f,0.5f,1.f}, "(Press Play to simulate)");
        ImGui::Spacing();
        for (size_t i = 0; i < editorScene.entities.size(); ++i) {
            const auto& ent = editorScene.entities[i];
            bool isPlayer = (ent.type == EntityData::Type::Player);
            ImVec4 col = isPlayer ? ImVec4(0.4f,0.7f,1.f,1.f) : ImVec4(1.f,0.4f,0.4f,1.f);
            const char* icon = isPlayer ? "[P]" : "[E]";

            ImGui::TextColored(col, "%s", icon);
            ImGui::SameLine();
            bool selected = (selectedEntity_ == static_cast<int>(i));
            if (ImGui::Selectable(ent.name.c_str(), selected)) {
                selectedEntity_ = static_cast<int>(i);
                camFocusX_ = ent.x * TILE_SCALE;
                camFocusZ_ = ent.y * TILE_SCALE;
                camFocusY_ = 0.f;
                camDistance_ = 15.f;  // zoom in to see entity
            }
            // Tooltip with details
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("Position: (%.1f, %.1f)", ent.x, ent.y);
                ImGui::Text("Type: %s", isPlayer ? "Player" : "Enemy");
                for (const auto& cv : ent.components) {
                    if (auto* hc = std::get_if<HealthComponent>(&cv))
                        ImGui::Text("HP: %d/%d", hc->health, hc->maxHealth);
                    if (auto* sc = std::get_if<StatsComponent>(&cv))
                        ImGui::Text("ATK:%d DEF:%d SPD:%d", sc->attack, sc->defense, sc->speed);
                    if (auto* ac = std::get_if<AIComponent>(&cv))
                        ImGui::Text("AI: detect=%.0f", ac->detectionRange);
                }
                ImGui::EndTooltip();
            }
        }
    } else {
        // Show live simulation entities
        // Player
        {
            ImVec4 col = simPlayer_.isAlive() ? ImVec4(0.4f,0.7f,1.f,1.f) : ImVec4(0.3f,0.3f,0.3f,1.f);
            ImGui::TextColored(col, "[P]");
            ImGui::SameLine();
            bool selected = (selectedEntity_ == -1);
            char label[128];
            snprintf(label, sizeof(label), "%s##player", simPlayer_.name.c_str());
            if (ImGui::Selectable(label, selected)) {
                selectedEntity_ = -1;
                camFocusX_ = simPlayer_.x * TILE_SCALE;
                camFocusZ_ = simPlayer_.y * TILE_SCALE;
                camFocusY_ = 0.f;
                camDistance_ = 15.f;
            }
            // Health bar inline
            if (simPlayer_.maxHealth > 0) {
                float ratio = static_cast<float>(simPlayer_.health) / simPlayer_.maxHealth;
                ImGui::SameLine(listWidth - 55.f);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                    ratio > 0.5f ? ImVec4(0.2f,0.8f,0.2f,1.f) :
                    ratio > 0.25f ? ImVec4(0.8f,0.8f,0.2f,1.f) :
                                    ImVec4(0.8f,0.2f,0.2f,1.f));
                ImGui::ProgressBar(ratio, {50.f, 12.f}, "");
                ImGui::PopStyleColor();
            }
        }

        ImGui::Separator();
        ImGui::Text("Enemies (%d)", static_cast<int>(simEnemies_.size()));
        ImGui::Separator();

        for (size_t i = 0; i < simEnemies_.size(); ++i) {
            const auto& e = simEnemies_[i];
            bool alive = e->isAlive();
            ImVec4 col = alive ? ImVec4(1.f,0.4f,0.4f,1.f) : ImVec4(0.3f,0.3f,0.3f,1.f);

            ImGui::TextColored(col, "[E]");
            ImGui::SameLine();
            bool selected = (selectedEntity_ == static_cast<int>(i));
            char label[128];
            snprintf(label, sizeof(label), "%s##enemy%d", e->name.c_str(), static_cast<int>(i));
            if (ImGui::Selectable(label, selected)) {
                selectedEntity_ = static_cast<int>(i);
                camFocusX_ = e->x * TILE_SCALE;
                camFocusZ_ = e->y * TILE_SCALE;
                camFocusY_ = 0.f;
                camDistance_ = 15.f;
            }
            // Health bar inline
            if (e->maxHealth > 0) {
                float ratio = static_cast<float>(e->health) / e->maxHealth;
                ImGui::SameLine(listWidth - 55.f);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                    alive ? (ratio > 0.5f ? ImVec4(0.2f,0.8f,0.2f,1.f) :
                            ratio > 0.25f ? ImVec4(0.8f,0.8f,0.2f,1.f) :
                                            ImVec4(0.8f,0.2f,0.2f,1.f))
                          : ImVec4(0.3f,0.3f,0.3f,1.f));
                ImGui::ProgressBar(ratio, {50.f, 12.f}, "");
                ImGui::PopStyleColor();
            }
            if (ImGui::IsItemHovered() && alive) {
                ImGui::BeginTooltip();
                ImGui::Text("Pos: (%.1f, %.1f)", e->x, e->y);
                ImGui::Text("HP: %d/%d", e->health, e->maxHealth);
                ImGui::Text("ATK: %d  DEF: %d", e->stats.attack, e->stats.defense);
                ImGui::Text("Detection: %.0f", e->detectionRadius);
                ImGui::EndTooltip();
            }
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ── Viewport image ───────────────────────────────────────────────────────
    ImGui::BeginChild("##ViewportArea", { vpWidth, avail.y }, false);
    ImVec2 vpAvail = ImGui::GetContentRegionAvail();
    if (vpAvail.x < 1.f) vpAvail.x = 1.f;
    if (vpAvail.y < 1.f) vpAvail.y = 1.f;

    float texAspect = static_cast<float>(texW_) / texH_;
    float winAspect = vpAvail.x / vpAvail.y;
    float imgW, imgH;
    if (winAspect > texAspect) {
        imgH = vpAvail.y;
        imgW = imgH * texAspect;
    } else {
        imgW = vpAvail.x;
        imgH = imgW / texAspect;
    }

    float offsetX = (vpAvail.x - imgW) * 0.5f;
    float offsetY = (vpAvail.y - imgH) * 0.5f;
    ImVec2 cursorPos = ImGui::GetCursorPos();
    ImGui::SetCursorPos({ cursorPos.x + offsetX, cursorPos.y + offsetY });

    ImGui::Image((ImTextureID)texture_, { imgW, imgH });

    // ── 3D Navigation ────────────────────────────────────────────────────────
    bool hovered = ImGui::IsItemHovered();
    bool focused = ImGui::IsWindowFocused();

    if (hovered) {
        // ── Orbit: Right-drag ────────────────────────────────────────────
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            camYaw_   -= delta.x * 0.3f;
            camPitch_ += delta.y * 0.3f;
            camPitch_ = std::max(-89.f, std::min(89.f, camPitch_));
        }

        // ── Pan: Middle-drag ─────────────────────────────────────────────
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            float yawRad = camYaw_ * kDeg2Rad;
            float panSpeed = camDistance_ * 0.004f;
            // Camera right vector (on XZ plane)
            float camRX = std::cos(yawRad);
            float camRZ = -std::sin(yawRad);
            // Camera up projected to world (approximation for panning)
            float camUpX = -std::sin(yawRad) * std::sin(camPitch_ * kDeg2Rad);
            float camUpZ = -std::cos(yawRad) * std::sin(camPitch_ * kDeg2Rad);
            float camUpY = std::cos(camPitch_ * kDeg2Rad);

            camFocusX_ -= (camRX * delta.x + camUpX * delta.y) * panSpeed;
            camFocusY_ += camUpY * delta.y * panSpeed;
            camFocusZ_ -= (camRZ * delta.x + camUpZ * delta.y) * panSpeed;
        }

        // ── Zoom: Scroll wheel ───────────────────────────────────────────
        float scroll = ImGui::GetIO().MouseWheel;
        if (std::abs(scroll) > 0.01f) {
            camDistance_ -= scroll * camDistance_ * 0.12f;
            camDistance_ = std::max(2.f, std::min(600.f, camDistance_));
        }

        // ── Left click: player move target ───────────────────────────────
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && simState_ == SimState::Playing) {
            ImVec2 mousePos = ImGui::GetMousePos();
            ImVec2 imgPos = ImGui::GetItemRectMin();
            float mx = (mousePos.x - imgPos.x) / imgW;
            float my = (mousePos.y - imgPos.y) / imgH;
            if (mx >= 0.f && mx <= 1.f && my >= 0.f && my <= 1.f) {
                float ndcX = mx * 2.f - 1.f;
                float ndcY = 1.f - my * 2.f;

                float yawRad = camYaw_ * kDeg2Rad;
                float pitchRad = camPitch_ * kDeg2Rad;
                float fovRad = camFOV_ * kDeg2Rad * 0.5f;
                float aspect = static_cast<float>(texW_) / texH_;

                float fx = -std::sin(yawRad) * std::cos(pitchRad);
                float fy = -std::sin(pitchRad);
                float fz = -std::cos(yawRad) * std::cos(pitchRad);
                float rrx = std::cos(yawRad);
                float rrz = -std::sin(yawRad);
                float uux = -std::sin(yawRad) * (-std::sin(pitchRad));
                float uuy = std::cos(pitchRad);
                float uuz = -std::cos(yawRad) * (-std::sin(pitchRad));

                float tanFov = std::tan(fovRad);
                float dx = fx + rrx * ndcX * tanFov * aspect + uux * ndcY * tanFov;
                float dy = fy + uuy * ndcY * tanFov;
                float dz = fz + rrz * ndcX * tanFov * aspect + uuz * ndcY * tanFov;

                if (std::abs(dy) > 1e-4f) {
                    float t = -camEyeY_ / dy;
                    if (t > 0.f) {
                        float hitX = camEyeX_ + t * dx;
                        float hitZ = camEyeZ_ + t * dz;
                        simPlayer_.setMoveTarget(hitX / TILE_SCALE, hitZ / TILE_SCALE);
                    }
                }
            }
        }
    }

    // ── WASD navigation (when window is focused and hovered) ─────────────
    if (focused && hovered) {
        float dt = ImGui::GetIO().DeltaTime;
        float moveSpeed = camDistance_ * 0.8f * dt;
        float yawRad = camYaw_ * kDeg2Rad;

        // Forward/back direction on XZ plane
        float fwdX = -std::sin(yawRad);
        float fwdZ = -std::cos(yawRad);
        // Right direction
        float rgtX = std::cos(yawRad);
        float rgtZ = -std::sin(yawRad);

        if (ImGui::IsKeyDown(ImGuiKey_W)) {
            camFocusX_ += fwdX * moveSpeed;
            camFocusZ_ += fwdZ * moveSpeed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_S)) {
            camFocusX_ -= fwdX * moveSpeed;
            camFocusZ_ -= fwdZ * moveSpeed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_A)) {
            camFocusX_ -= rgtX * moveSpeed;
            camFocusZ_ -= rgtZ * moveSpeed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_D)) {
            camFocusX_ += rgtX * moveSpeed;
            camFocusZ_ += rgtZ * moveSpeed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) {
            camFocusY_ -= moveSpeed;
        }
        if (ImGui::IsKeyDown(ImGuiKey_E)) {
            camFocusY_ += moveSpeed;
        }
    }

    // ── Navigation help tooltip ──────────────────────────────────────────
    if (hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        ImGui::SetTooltip("Orbit: RMB drag | Pan: MMB drag\n"
                          "Zoom: Scroll | WASD: Move | Q/E: Up/Down");
    }

    ImGui::EndChild(); // ##ViewportArea

    ImGui::End();
}
