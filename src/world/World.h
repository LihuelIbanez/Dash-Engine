#pragma once
#include <SDL2/SDL.h>
#include <array>
#include <vector>
#include "IsoRenderer.h"
#include "TerrainMesh.h"

// ─────────────────────────────────────────────────────────────────────────────
// Tile – one cell in the world grid (legacy, kept for backward compatibility)
// ─────────────────────────────────────────────────────────────────────────────
struct Tile {
    TileType type     = TileType::Grass;
    bool     walkable = true;

    SDL_Color topColor()  const;   // lighter face colour
    SDL_Color sideColor() const;   // darker variant for outline / wall

    bool operator==(const Tile& o) const { return type == o.type && walkable == o.walkable; }
    bool operator!=(const Tile& o) const { return !(*this == o); }
};

// ─────────────────────────────────────────────────────────────────────────────
// World – the static tile map + polygon terrain mesh
// ─────────────────────────────────────────────────────────────────────────────
class World {
public:
    // Legacy grid (kept for backward compat during transition)
    std::vector<std::array<Tile, WORLD_W>> grid;

    World();

    // Procedurally fill both the legacy grid and the terrain mesh
    void generate(unsigned int seed = 42, const dash::world::BiomeTable* biomes = nullptr);

    // Draw all tiles (painter order: top → bottom) — legacy 2D
    void draw(SDL_Renderer* renderer, float camX, float camY) const;

    // Draw terrain mesh as triangles (SDL2)
    void drawMesh(SDL_Renderer* renderer, float camX, float camY) const;

    // Query walkability for a world-space float position
    bool isWalkable(float wx, float wy) const;

    // Movement cost for a tile (used by A* pathfinding)
    float terrainCost(int tx, int ty) const;

    // Access the polygon terrain mesh
    TerrainMesh&       terrain()       { return terrain_; }
    const TerrainMesh& terrain() const { return terrain_; }

private:
    TerrainMesh terrain_;

    void drawTile(SDL_Renderer* renderer,
                  int tx, int ty,
                  float camX, float camY) const;
};
