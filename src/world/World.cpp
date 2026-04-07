#include "World.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

// ═════════════════════════════════════════════════════════════════════════════
// Perlin Noise – self-contained 2D implementation (no dependencies)
// ═════════════════════════════════════════════════════════════════════════════
namespace {

// Permutation table – shuffled by seed before use
static int perm_[512];

void initPerm(unsigned int seed)
{
    // Fill with identity
    for (int i = 0; i < 256; ++i) perm_[i] = i;

    // Fisher–Yates shuffle seeded by our parameter
    unsigned int s = seed;
    for (int i = 255; i > 0; --i) {
        s = s * 1664525u + 1013904223u;          // LCG
        int j = static_cast<int>((s >> 16) % (i + 1));
        int tmp  = perm_[i];
        perm_[i] = perm_[j];
        perm_[j] = tmp;
    }
    // Duplicate for wrapping
    for (int i = 0; i < 256; ++i) perm_[256 + i] = perm_[i];
}

inline float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
inline float lerp(float a, float b, float t) { return a + t * (b - a); }

float grad(int hash, float x, float y)
{
    switch (hash & 3) {
    case 0: return  x + y;
    case 1: return -x + y;
    case 2: return  x - y;
    default: return -x - y;
    }
}

float perlin(float x, float y)
{
    int xi = static_cast<int>(std::floor(x)) & 255;
    int yi = static_cast<int>(std::floor(y)) & 255;

    float xf = x - std::floor(x);
    float yf = y - std::floor(y);

    float u = fade(xf);
    float v = fade(yf);

    int aa = perm_[perm_[xi]     + yi];
    int ab = perm_[perm_[xi]     + yi + 1];
    int ba = perm_[perm_[xi + 1] + yi];
    int bb = perm_[perm_[xi + 1] + yi + 1];

    float x1 = lerp(grad(aa, xf,       yf),
                     grad(ba, xf - 1.f, yf),       u);
    float x2 = lerp(grad(ab, xf,       yf - 1.f),
                     grad(bb, xf - 1.f, yf - 1.f), u);

    return lerp(x1, x2, v);   // range roughly [-1, 1]
}

// Fractal Brownian Motion – stack multiple octaves for natural detail
float fbm(float x, float y, int octaves, float lacunarity, float gain)
{
    float sum   = 0.f;
    float amp   = 1.f;
    float freq  = 1.f;
    float maxAmp = 0.f;

    for (int i = 0; i < octaves; ++i) {
        sum    += perlin(x * freq, y * freq) * amp;
        maxAmp += amp;
        amp    *= gain;
        freq   *= lacunarity;
    }
    return sum / maxAmp;   // normalised to roughly [-1, 1]
}

// Map [-1,1] → [0,1]
inline float norm01(float v) { return (v + 1.f) * 0.5f; }

// Island falloff — edges of the map sink below water
float islandMask(int col, int row, int w, int h)
{
    float nx = 2.f * col / (w - 1) - 1.f;   // -1..1
    float ny = 2.f * row / (h - 1) - 1.f;
    float d  = std::max(std::abs(nx), std::abs(ny));  // square distance
    // Smooth step that starts dropping at d~0.6
    float t = std::max(0.f, (d - 0.55f) / 0.45f);
    return 1.f - t * t;
}

} // anonymous namespace

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
    generate();
}

// ═════════════════════════════════════════════════════════════════════════════
// Realistic procedural generation
//
// Algorithm:
//   1. Two noise maps: ELEVATION and MOISTURE, each using fBm Perlin noise.
//   2. Elevation is multiplied by an island mask that sinks edges → ocean.
//   3. Biome is selected from an elevation × moisture lookup table:
//
//        elevation  ↑ Snow
//                   │ Mountain / Stone
//                   │ Forest (wet) │ Dirt (dry)
//                   │ Grass (mid)  │ Sand (dry)
//                   │ Water
//                   │ DeepWater
//                   └─────────────────────→ moisture
// ═════════════════════════════════════════════════════════════════════════════
void World::generate(unsigned int seed)
{
    // ── Elevation noise ──────────────────────────────────────────────────────
    initPerm(seed);
    float elev[WORLD_H][WORLD_W];

    const float scale = 0.07f;  // controls continent size

    for (int row = 0; row < WORLD_H; ++row) {
        for (int col = 0; col < WORLD_W; ++col) {
            float e = fbm(col * scale, row * scale, 6, 2.0f, 0.5f);
            e = norm01(e);
            e *= islandMask(col, row, WORLD_W, WORLD_H);
            elev[row][col] = e;
        }
    }

    // ── Moisture noise (different seed offset) ───────────────────────────────
    initPerm(seed + 31337u);
    float moist[WORLD_H][WORLD_W];

    for (int row = 0; row < WORLD_H; ++row) {
        for (int col = 0; col < WORLD_W; ++col) {
            float m = fbm(col * 0.09f + 100.f, row * 0.09f + 100.f, 4, 2.0f, 0.5f);
            moist[row][col] = norm01(m);
        }
    }

    // ── Detail noise for roughness / variation ───────────────────────────────
    initPerm(seed + 77777u);

    // ── Biome assignment ─────────────────────────────────────────────────────
    for (int row = 0; row < WORLD_H; ++row) {
        for (int col = 0; col < WORLD_W; ++col) {
            float e = elev[row][col];
            float m = moist[row][col];
            float detail = norm01(perlin(col * 0.25f, row * 0.25f));

            Tile& t = grid[row][col];
            t.walkable = true;

            // Force water border around the map
            if (col == 0 || col == WORLD_W - 1 ||
                row == 0 || row == WORLD_H - 1)
            {
                t.type = TileType::Water;
                t.walkable = false;
                continue;
            }

            // Elevation thresholds
            if (e < 0.22f) {
                // Deep ocean
                t.type = TileType::DeepWater;
                t.walkable = false;
            }
            else if (e < 0.32f) {
                // Shallow water
                t.type = TileType::Water;
                t.walkable = false;
            }
            else if (e < 0.37f) {
                // Beach / shore
                t.type = TileType::Sand;
            }
            else if (e < 0.60f) {
                // Lowlands — biome depends on moisture
                if (m > 0.60f) {
                    t.type = TileType::Forest;
                } else if (m > 0.40f) {
                    t.type = TileType::Grass;
                } else if (m > 0.25f) {
                    t.type = (detail > 0.5f) ? TileType::Dirt : TileType::Grass;
                } else {
                    t.type = (detail > 0.4f) ? TileType::Sand : TileType::Dirt;
                }
            }
            else if (e < 0.72f) {
                // Highlands
                if (m > 0.55f) {
                    t.type = TileType::Forest;
                } else if (m > 0.35f) {
                    t.type = (detail > 0.6f) ? TileType::Dirt : TileType::Grass;
                } else {
                    t.type = TileType::Stone;
                }
            }
            else if (e < 0.82f) {
                // Mountains
                t.type = TileType::Mountain;
                t.walkable = (detail > 0.4f);  // some mountain tiles block
            }
            else if (e < 0.88f) {
                // High mountains
                t.type = TileType::Stone;
                t.walkable = false;
            }
            else {
                // Peaks → snow
                t.type = TileType::Snow;
                t.walkable = false;
            }
        }
    }

    // ── Post-processing: smooth isolated tiles ───────────────────────────────
    // Remove single-tile water pools / land dots for cleaner coastlines
    for (int pass = 0; pass < 2; ++pass) {
        for (int row = 1; row < WORLD_H - 1; ++row) {
            for (int col = 1; col < WORLD_W - 1; ++col) {
                bool isWater = (grid[row][col].type == TileType::Water ||
                                grid[row][col].type == TileType::DeepWater);

                // Count water neighbours (4-connected)
                int wn = 0;
                if (grid[row-1][col].type == TileType::Water ||
                    grid[row-1][col].type == TileType::DeepWater) ++wn;
                if (grid[row+1][col].type == TileType::Water ||
                    grid[row+1][col].type == TileType::DeepWater) ++wn;
                if (grid[row][col-1].type == TileType::Water ||
                    grid[row][col-1].type == TileType::DeepWater) ++wn;
                if (grid[row][col+1].type == TileType::Water ||
                    grid[row][col+1].type == TileType::DeepWater) ++wn;

                // Isolated water tile surrounded by land → make sand
                if (isWater && wn <= 1) {
                    grid[row][col].type = TileType::Sand;
                    grid[row][col].walkable = true;
                }
                // Isolated land tile surrounded by water → make water
                if (!isWater && wn >= 3) {
                    grid[row][col].type = TileType::Water;
                    grid[row][col].walkable = false;
                }
            }
        }
    }

    // ── Ensure sand strip along coastlines ───────────────────────────────────
    for (int row = 1; row < WORLD_H - 1; ++row) {
        for (int col = 1; col < WORLD_W - 1; ++col) {
            if (grid[row][col].type != TileType::Water &&
                grid[row][col].type != TileType::DeepWater &&
                grid[row][col].type != TileType::Sand)
            {
                // Check if any neighbour is water
                bool nearWater = false;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (grid[row+dy][col+dx].type == TileType::Water ||
                            grid[row+dy][col+dx].type == TileType::DeepWater)
                            nearWater = true;

                if (nearWater)
                    grid[row][col].type = TileType::Sand;
            }
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
    // Painter's algorithm: iterate by increasing (row + col) depth,
    // so tiles farther from camera are drawn first.
    for (int depth = 0; depth < WORLD_W + WORLD_H - 1; ++depth) {
        int colStart = std::max(0, depth - (WORLD_H - 1));
        int colEnd   = std::min(WORLD_W - 1, depth);
        for (int col = colStart; col <= colEnd; ++col) {
            int row = depth - col;
            if (row >= 0 && row < WORLD_H)
                drawTile(renderer, col, row, camX, camY);
        }
    }
}
