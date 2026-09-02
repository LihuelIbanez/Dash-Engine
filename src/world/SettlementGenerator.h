#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// SettlementGenerator — procedural placement of settlements (town anchor
// points) plus a road network connecting them across the walkable terrain.
//
// Pure algorithm, no SceneData/ImGui dependency: it only reads `World`
// (isWalkable/terrainCost via GridNav) and returns plain data. Turning the
// result into actual scene entities + TileOverrides is a separate step
// (GenerateSettlementsCommand, src/editor/commands/), the same split
// BiomeDesignerPanel uses between "compute" and "author".
// ─────────────────────────────────────────────────────────────────────────────

#include "game/nav/GridNav.h"

#include <string>
#include <vector>

class World;

struct Settlement {
    float       x = 0.f;
    float       y = 0.f;
    std::string name;
};

// A walkable path (tile centres, in world space via GridNav::tileToCentre)
// connecting two settlements.
struct SettlementRoad {
    std::vector<NavPoint> waypoints;
};

struct SettlementGenerationResult {
    std::vector<Settlement>    settlements;
    std::vector<SettlementRoad> roads;
};

struct SettlementGenerationParams {
    int          count       = 4;     // settlements to place
    float        minSpacing  = 24.f;  // minimum distance between settlements (world units)
    float        margin      = 12.f;  // keep this far from the map edges
    unsigned int seed        = 1;
    int          maxAttempts = 4000;  // rejection-sampling budget before giving up
};

// Rejection-samples `params.count` walkable, non-water/mountain spots at least
// `minSpacing` apart, then connects them with a nearest-neighbor minimum
// spanning tree (Prim's), routing each edge through GridNav::findPath so roads
// respect terrain cost/obstacles instead of cutting straight lines.
SettlementGenerationResult generateSettlements(const World& world,
                                               const SettlementGenerationParams& params);
