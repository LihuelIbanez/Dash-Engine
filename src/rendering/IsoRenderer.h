#pragma once
#include <SDL2/SDL.h>
#include <cmath>
#include <algorithm>

// ─── Tile dimensions ─────────────────────────────────────────────────────────
constexpr int TILE_W  = 64;   // width  of one tile diamond (pixels)
constexpr int TILE_H  = 32;   // height of one tile diamond (pixels)

// ─── Window / viewport ───────────────────────────────────────────────────────
constexpr int SCREEN_W = 1280;
constexpr int SCREEN_H = 720;

// ─── World dimensions ─────────────────────────────────────────────────────────
constexpr int WORLD_W = 64;
constexpr int WORLD_H = 64;

// ─── 2-D vector helpers ───────────────────────────────────────────────────────
struct Vec2f { float x, y; };
struct Vec2i { int   x, y; };

inline float vec2fLen(Vec2f v) { return std::sqrt(v.x * v.x + v.y * v.y); }
inline Vec2f vec2fNorm(Vec2f v) {
    float len = vec2fLen(v);
    if (len < 1e-4f) return {0.f, 0.f};
    return {v.x / len, v.y / len};
}

// ─── World → Screen conversion ────────────────────────────────────────────────
// The camera is always centred on (camX, camY) in world-space.
//   screenX = (relX - relY) * TILE_W/2  + SCREEN_W/2
//   screenY = (relX + relY) * TILE_H/2  + SCREEN_H/2
inline Vec2f worldToScreen(float wx, float wy, float camX, float camY)
{
    float rx = wx - camX;
    float ry = wy - camY;
    return {
        (rx - ry) * (TILE_W * 0.5f) + SCREEN_W * 0.5f,
        (rx + ry) * (TILE_H * 0.5f) + SCREEN_H * 0.5f
    };
}

// ─── Draw a filled isometric diamond ─────────────────────────────────────────
// cx, cy = screen centre of the diamond
// topCol  = colour of the top face  (lighter)
// sideCol = colour of the left/right face walls (darker) — unused on a flat tile
//           but kept so you can pass different colours when adding height later
inline void drawDiamond(SDL_Renderer* r,
                        float cx, float cy,
                        SDL_Color topCol,
                        SDL_Color sideCol)
{
    const float hw = TILE_W * 0.5f;
    const float hh = TILE_H * 0.5f;

    // Two triangles that make up the diamond
    //        top
    //       /   \
    //    left   right
    //       \   /
    //       bottom
    SDL_Vertex verts[4] = {
        { {cx,      cy - hh}, topCol,  {0,0} },   // 0 top
        { {cx + hw, cy      }, sideCol, {0,0} },   // 1 right
        { {cx,      cy + hh}, sideCol, {0,0} },   // 2 bottom
        { {cx - hw, cy      }, topCol,  {0,0} },   // 3 left
    };
    const int indices[6] = { 0,1,3,  1,2,3 };
    SDL_RenderGeometry(r, nullptr, verts, 4, indices, 6);
}

// ─── Draw a diamond outline (border) ─────────────────────────────────────────
inline void drawDiamondOutline(SDL_Renderer* r,
                               float cx, float cy,
                               Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha)
{
    float hw = TILE_W * 0.5f;
    float hh = TILE_H * 0.5f;

    SDL_SetRenderDrawColor(r, red, green, blue, alpha);
    SDL_FPoint pts[5] = {
        { cx,      cy - hh },
        { cx + hw, cy      },
        { cx,      cy + hh },
        { cx - hw, cy      },
        { cx,      cy - hh },   // close
    };
    SDL_RenderDrawLinesF(r, pts, 5);
}

// ─── SDL_Color helpers ────────────────────────────────────────────────────────
inline SDL_Color colDarken(SDL_Color c, int amount)
{
    auto clamp = [](int v) -> Uint8 {
        return static_cast<Uint8>(std::max(0, std::min(255, v)));
    };
    return { clamp(c.r - amount), clamp(c.g - amount),
             clamp(c.b - amount), c.a };
}
