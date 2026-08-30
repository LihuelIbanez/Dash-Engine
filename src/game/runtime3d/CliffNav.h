#pragma once

#include <algorithm>

#include "game/nav/GridNav.h"
#include "game/runtime3d/AgentAI.h"
#include "world/TerrainMesh.h"

// ─────────────────────────────────────────────────────────────────────────────
// CliffNav — makes the 2D tile A* aware of the 3D terrain's cliff tiers.
//
// GridNav walks World::grid, which only knows "walkable / not walkable"; the
// cliff levels live in TerrainMesh. Rather than baking terrain data into the
// grid (it would go stale the moment the terrain is edited) this builds a
// GridNav::StepFilter over the very TerrainMesh the renderer draws, so nav and
// geometry cannot disagree.
//
// Cliff levels are per vertex, so a face whose corners sit on different tiers
// *is* the wall quad; a face with four equal corners is flat ground.
// ─────────────────────────────────────────────────────────────────────────────

namespace dash::runtime3d {

inline bool tileInBounds(int tx, int ty)
{
    return tx >= 0 && tx < TerrainMesh::FW && ty >= 0 && ty < TerrainMesh::FH;
}

// Highest tier touched by the tile. Taking the max means walking onto a wall
// quad from below counts as a full climb, and off it as a full drop.
inline int tileCliffLevel(const TerrainMesh& terrain, int tx, int ty)
{
    if (!tileInBounds(tx, ty)) return 0;
    return static_cast<int>(std::max({terrain.cliffLevel(tx,     ty),
                                      terrain.cliffLevel(tx + 1, ty),
                                      terrain.cliffLevel(tx,     ty + 1),
                                      terrain.cliffLevel(tx + 1, ty + 1)}));
}

// How many tiers the tile itself spans: 0 on flat ground, N on a wall quad.
inline int tileCliffSpan(const TerrainMesh& terrain, int tx, int ty)
{
    if (!tileInBounds(tx, ty)) return 0;
    const uint8_t a = terrain.cliffLevel(tx,     ty);
    const uint8_t b = terrain.cliffLevel(tx + 1, ty);
    const uint8_t c = terrain.cliffLevel(tx,     ty + 1);
    const uint8_t d = terrain.cliffLevel(tx + 1, ty + 1);
    return static_cast<int>(std::max({a, b, c, d})) -
           static_cast<int>(std::min({a, b, c, d}));
}

// A step is legal when the destination is a slope this archetype could handle
// and the tier change from the source is within its climb / drop limits. With
// the defaults (0, 0) a cliff is a sheer wall: CLIFF_STEP is 12 world units
// across a one-unit tile, so nothing climbs it and nothing survives the fall.
inline bool cliffStepPassable(const TerrainMesh& terrain,
                              int fromX, int fromY, int toX, int toY,
                              int maxClimb = 0, int maxDrop = 0)
{
    if (!tileInBounds(toX, toY)) return false;
    if (tileCliffSpan(terrain, toX, toY) > std::max(maxClimb, maxDrop)) return false;
    return cliffStepAllowed(tileCliffLevel(terrain, fromX, fromY),
                            tileCliffLevel(terrain, toX, toY),
                            maxClimb, maxDrop);
}

// The terrain has to outlive the filter; the simulation owns both.
inline GridNav::StepFilter cliffStepFilter(const TerrainMesh& terrain,
                                           int maxClimb = 0, int maxDrop = 0)
{
    return [&terrain, maxClimb, maxDrop](int fromX, int fromY, int toX, int toY) {
        return cliffStepPassable(terrain, fromX, fromY, toX, toY, maxClimb, maxDrop);
    };
}

} // namespace dash::runtime3d
