#pragma once
#include <functional>
#include <vector>

class World;

// ─────────────────────────────────────────────────────────────────────────────
// GridNav – A* pathfinding on the tile grid
// ─────────────────────────────────────────────────────────────────────────────

struct NavPoint {
    int x, y;
};

class GridNav {
public:
    // Optional veto on a single tile-to-tile move, for rules the 2D grid cannot
    // express (cliff levels in the 3D terrain, faction blockers, …). Diagonal
    // moves are only taken when both cardinal legs pass it as well.
    using StepFilter = std::function<bool(int fromX, int fromY, int toX, int toY)>;

    // Find a path from (sx,sy) to (gx,gy) in tile coordinates.
    // Returns a list of waypoints (tile centres) from start to goal,
    // or an empty vector if no path exists.
    // maxSteps limits the search to avoid stalling on huge maps.
    static std::vector<NavPoint> findPath(int sx, int sy,
                                          int gx, int gy,
                                          const World& world,
                                          int maxSteps = 2048,
                                          const StepFilter& stepFilter = StepFilter{});

    // Convert float world position to tile coordinate (clamped)
    static NavPoint worldToTile(float wx, float wy);

    // Convert tile coordinate to float world centre
    static void tileToCentre(int tx, int ty, float& wx, float& wy);
};
