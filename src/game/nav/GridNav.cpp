#include "GridNav.h"
#include "World.h"
#include <queue>
#include <vector>
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Internal A* implementation
// ─────────────────────────────────────────────────────────────────────────────
namespace {

struct Node {
    int   x, y;
    float g;      // cost from start
    float f;      // g + heuristic
};

struct NodeCmp {
    bool operator()(const Node& a, const Node& b) const { return a.f > b.f; }
};

// 8-directional neighbours (cardinal + diagonal)
constexpr int DX[8] = { 1, -1, 0,  0, 1, -1, 1, -1 };
constexpr int DY[8] = { 0,  0, 1, -1, 1, -1, -1, 1 };

inline float heuristic(int ax, int ay, int bx, int by)
{
    // Octile distance (consistent heuristic for 8-dir movement)
    float dx = static_cast<float>(std::abs(ax - bx));
    float dy = static_cast<float>(std::abs(ay - by));
    return (dx + dy) + (1.41421356f - 2.f) * std::min(dx, dy);
}

inline int idx(int x, int y) { return y * WORLD_W + x; }

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

NavPoint GridNav::worldToTile(float wx, float wy)
{
    int tx = std::clamp(static_cast<int>(wx), 0, WORLD_W - 1);
    int ty = std::clamp(static_cast<int>(wy), 0, WORLD_H - 1);
    return { tx, ty };
}

void GridNav::tileToCentre(int tx, int ty, float& wx, float& wy)
{
    wx = static_cast<float>(tx) + 0.5f;
    wy = static_cast<float>(ty) + 0.5f;
}

std::vector<NavPoint> GridNav::findPath(int sx, int sy,
                                        int gx, int gy,
                                        const World& world,
                                        int maxSteps)
{
    // Clamp to grid bounds
    sx = std::clamp(sx, 0, WORLD_W - 1);
    sy = std::clamp(sy, 0, WORLD_H - 1);
    gx = std::clamp(gx, 0, WORLD_W - 1);
    gy = std::clamp(gy, 0, WORLD_H - 1);

    // Trivial cases
    if (sx == gx && sy == gy) return { {gx, gy} };
    if (!world.grid[gy][gx].walkable) return {};

    const int total = WORLD_W * WORLD_H;

    // Per-cell arrays (heap-allocated; WORLD_W * WORLD_H can exceed 64 KB)
    std::vector<float> bestG(total, 1e30f);
    std::vector<int>   parentX(total, -1);
    std::vector<int>   parentY(total, -1);
    std::vector<bool>  closed(total, false);

    // Open set (min-heap by f)
    std::priority_queue<Node, std::vector<Node>, NodeCmp> open;

    bestG[idx(sx, sy)] = 0.f;
    parentX[idx(sx, sy)] = -1;
    parentY[idx(sx, sy)] = -1;
    open.push({ sx, sy, 0.f, heuristic(sx, sy, gx, gy) });

    int steps = 0;
    bool found = false;

    while (!open.empty() && steps < maxSteps) {
        Node cur = open.top(); open.pop();
        int ci = idx(cur.x, cur.y);

        if (closed[ci]) continue;
        closed[ci] = true;
        ++steps;

        // Goal reached
        if (cur.x == gx && cur.y == gy) { found = true; break; }

        for (int d = 0; d < 8; ++d) {
            int nx = cur.x + DX[d];
            int ny = cur.y + DY[d];
            if (nx < 0 || nx >= WORLD_W || ny < 0 || ny >= WORLD_H) continue;
            if (closed[idx(nx, ny)]) continue;
            if (!world.grid[ny][nx].walkable) continue;

            // For diagonal moves, ensure both adjacent cardinal tiles are walkable
            // (prevents cutting through corners of walls)
            if (DX[d] != 0 && DY[d] != 0) {
                if (!world.grid[cur.y][nx].walkable ||
                    !world.grid[ny][cur.x].walkable)
                    continue;
            }

            // Movement cost: cardinal = terrain cost, diagonal = terrain cost * sqrt(2)
            float terrainCost = world.terrainCost(nx, ny);
            float moveCost = (DX[d] != 0 && DY[d] != 0)
                           ? terrainCost * 1.41421356f
                           : terrainCost;

            float newG = cur.g + moveCost;
            int ni = idx(nx, ny);
            if (newG < bestG[ni]) {
                bestG[ni]   = newG;
                parentX[ni] = cur.x;
                parentY[ni] = cur.y;
                open.push({ nx, ny, newG, newG + heuristic(nx, ny, gx, gy) });
            }
        }
    }

    if (!found) return {};

    // Reconstruct path (goal → start), then reverse
    std::vector<NavPoint> path;
    int cx = gx, cy = gy;
    while (cx != -1) {
        path.push_back({ cx, cy });
        int i = idx(cx, cy);
        int px = parentX[i];
        int py = parentY[i];
        cx = px; cy = py;
    }
    std::reverse(path.begin(), path.end());
    return path;
}
