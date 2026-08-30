#include "World.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

// ═════════════════════════════════════════════════════════════════════════════
// Tile colours – Diablo-dark palette with new biome types
// ═════════════════════════════════════════════════════════════════════════════
SDL_Color Tile::topColor() const
{
    switch (type) {
    case TileType::DeepWater: return {  10,  18,  55, 255 };
    case TileType::Water:     return {  20,  35,  80, 255 };
    case TileType::Sand:      return { 110,  90,  50, 255 };
    case TileType::Grass:     return {  35,  55,  25, 255 };
    case TileType::Forest:    return {  20,  40,  15, 255 };
    case TileType::Dirt:      return {  65,  42,  25, 255 };
    case TileType::Stone:     return {  70,  65,  60, 255 };
    case TileType::Mountain:  return {  55,  50,  48, 255 };
    case TileType::Snow:      return { 160, 165, 175, 255 };
    }
    return { 50, 50, 50, 255 };
}

SDL_Color Tile::sideColor() const
{
    SDL_Color t = topColor();
    return colDarken(t, 45);
}

// ═════════════════════════════════════════════════════════════════════════════
// World – construction
// ═════════════════════════════════════════════════════════════════════════════
World::World()
{
    grid.resize(WORLD_H);
    generate();
}

// ═════════════════════════════════════════════════════════════════════════════
// Realistic procedural generation
//
// The biome rule itself lives in TerrainMesh::generate (data-driven through
// assets/world/biomes.json, with the built-in thresholds as fallback). This used
// to be a second, hand-kept copy of the same cascade over its own noise, which
// let the nav grid and the rendered terrain drift apart; now the legacy tile
// grid is simply projected from the faces the renderer draws.
// ═════════════════════════════════════════════════════════════════════════════
void World::generate(unsigned int seed, const dash::world::BiomeTable* biomes)
{
    terrain_.generate(seed, biomes);

    for (int row = 0; row < WORLD_H; ++row) {
        for (int col = 0; col < WORLD_W; ++col) {
            const TerrainFace& f = terrain_.face(col, row);
            grid[row][col].type = f.type;
            grid[row][col].walkable = f.walkable;
        }
    }
}

bool World::isWalkable(float wx, float wy) const
{
    int col = static_cast<int>(wx);
    int row = static_cast<int>(wy);
    if (col < 0 || col >= WORLD_W || row < 0 || row >= WORLD_H) return false;
    return grid[row][col].walkable;
}

float World::terrainCost(int tx, int ty) const
{
    if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) return 1e6f;
    const Tile& t = grid[ty][tx];
    if (!t.walkable) return 1e6f;

    switch (t.type) {
    case TileType::Grass:   return 1.0f;
    case TileType::Dirt:    return 1.0f;
    case TileType::Sand:    return 1.3f;   // sand is slower
    case TileType::Forest:  return 1.5f;   // trees slow movement
    case TileType::Stone:   return 1.1f;
    case TileType::Mountain:return 2.0f;   // walkable mountain tiles are expensive
    case TileType::Snow:    return 1.8f;
    default:                return 1e6f;   // water / deep water
    }
}

// ─── Rendering ────────────────────────────────────────────────────────────────
void World::drawTile(SDL_Renderer* renderer,
                     int tx, int ty,
                     float camX, float camY) const
{
    const Tile& tile = grid[ty][tx];

    // Centre of the tile diamond in screen space
    // Tile world position is at (tx + 0.5, ty + 0.5) (centre of cell)
    Vec2f s = worldToScreen(tx + 0.5f, ty + 0.5f, camX, camY);

    SDL_Color top  = tile.topColor();
    SDL_Color side = tile.sideColor();

    drawDiamond(renderer, s.x, s.y, top, side);
    drawDiamondOutline(renderer, s.x, s.y,
                       side.r, side.g, side.b, 180);
}

void World::draw(SDL_Renderer* renderer, float camX, float camY) const
{
    // Coarse culling for large worlds: skip tiles far outside viewport footprint.
    const float maxIsoReach = (static_cast<float>(SCREEN_W) / TILE_W)
                            + (static_cast<float>(SCREEN_H) / TILE_H)
                            + 10.0f;

    // Painter's algorithm: iterate by increasing (row + col) depth,
    // so tiles farther from camera are drawn first.
    for (int depth = 0; depth < WORLD_W + WORLD_H - 1; ++depth) {
        int colStart = std::max(0, depth - (WORLD_H - 1));
        int colEnd   = std::min(WORLD_W - 1, depth);
        for (int col = colStart; col <= colEnd; ++col) {
            int row = depth - col;
            if (row >= 0 && row < WORLD_H) {
                const float dx = std::fabs((col + 0.5f) - camX);
                const float dy = std::fabs((row + 0.5f) - camY);
                if ((dx + dy) > maxIsoReach) continue;
                drawTile(renderer, col, row, camX, camY);
            }
        }
    }
}

// ─── Mesh-based terrain rendering (SDL2) ─────────────────────────────────────
void World::drawMesh(SDL_Renderer* renderer, float camX, float camY) const
{
    const float hw = TILE_W * 0.5f;
    const float hh = TILE_H * 0.5f;
    const float maxIsoReach = (static_cast<float>(SCREEN_W) / TILE_W)
                            + (static_cast<float>(SCREEN_H) / TILE_H)
                            + 10.0f;

    // Lambda: project a mesh vertex to screen coordinates
    auto project = [&](int vx, int vy) -> SDL_FPoint {
        float h = terrain_.worldHeight(vx, vy);
        float rx = (static_cast<float>(vx) - camX) * TILE_SCALE;
        float ry = (static_cast<float>(vy) - camY) * TILE_SCALE;
        float sx = (rx - ry) * hw + SCREEN_W * 0.5f;
        float sy = (rx + ry) * hh - (h * TILE_SCALE * 4.f) + SCREEN_H * 0.5f;
        return {sx, sy};
    };

    // Painter's order: iterate by depth (row + col)
    for (int depth = 0; depth < WORLD_W + WORLD_H - 1; ++depth) {
        int colStart = std::max(0, depth - (WORLD_H - 1));
        int colEnd   = std::min(WORLD_W - 1, depth);
        for (int col = colStart; col <= colEnd; ++col) {
            int row = depth - col;
            if (row < 0 || row >= WORLD_H) continue;

            float dx = std::fabs((col + 0.5f) - camX);
            float dy = std::fabs((row + 0.5f) - camY);
            if ((dx + dy) > maxIsoReach) continue;

            const TerrainFace& f = terrain_.face(col, row);
            SDL_Color top = f.topColor();
            SDL_Color dark = colDarken(top, 20);

            SDL_FPoint pTL = project(col,     row);
            SDL_FPoint pTR = project(col + 1, row);
            SDL_FPoint pBL = project(col,     row + 1);
            SDL_FPoint pBR = project(col + 1, row + 1);

            // Triangle 1: TL-TR-BL (lighter)
            SDL_Vertex tri1[3] = {
                {pTL, top,  {0, 0}},
                {pTR, top,  {0, 0}},
                {pBL, dark, {0, 0}},
            };
            SDL_RenderGeometry(renderer, nullptr, tri1, 3, nullptr, 0);

            // Triangle 2: TR-BR-BL (darker)
            SDL_Vertex tri2[3] = {
                {pTR, dark, {0, 0}},
                {pBR, dark, {0, 0}},
                {pBL, dark, {0, 0}},
            };
            SDL_RenderGeometry(renderer, nullptr, tri2, 3, nullptr, 0);
        }
    }
}
