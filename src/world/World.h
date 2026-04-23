#pragma once
#include <SDL2/SDL.h>
#include <array>
#include <vector>
#include "IsoRenderer.h"

// ─────────────────────────────────────────────────────────────────────────────
// TileType – easy to extend with more biomes / dungeon types
// ─────────────────────────────────────────────────────────────────────────────
enum class TileType {
    DeepWater,
    Water,
    Sand,
    Grass,
    Forest,
    Dirt,
    Stone,
    Mountain,
    Snow
};

// ─────────────────────────────────────────────────────────────────────────────
// Tile – one cell in the world grid
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
// World – the static tile map
// ─────────────────────────────────────────────────────────────────────────────
class World {
public:
    std::vector<std::array<Tile, WORLD_W>> grid;

    World();

    // Procedurally fill the grid
    void generate(unsigned int seed = 42);

    // Draw all tiles (painter order: top → bottom)
    void draw(SDL_Renderer* renderer, float camX, float camY) const;

    // Query walkability for a world-space float position
    bool isWalkable(float wx, float wy) const;

    // Movement cost for a tile (used by A* pathfinding).
    // Returns a high value for non-walkable tiles.
    float terrainCost(int tx, int ty) const;

private:
    void drawTile(SDL_Renderer* renderer,
                  int tx, int ty,
                  float camX, float camY) const;
};
