#include "SettlementGenerator.h"

#include "World.h"

#include <cmath>
#include <random>

namespace {

bool isGoodSettlementSpot(const World& world, float x, float y)
{
    if (!world.isWalkable(x, y)) return false;

    const int tx = static_cast<int>(x);
    const int ty = static_cast<int>(y);
    if (tx < 0 || tx >= WORLD_W || ty < 0 || ty >= WORLD_H) return false;

    // Water/mountain/snow already fail isWalkable in practice, but a settlement
    // also should not spawn on loose sand or deep forest undergrowth.
    const TileType t = world.grid[static_cast<std::size_t>(ty)][static_cast<std::size_t>(tx)].type;
    return t == TileType::Grass || t == TileType::Dirt;
}

float distance(const Settlement& a, const Settlement& b)
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

SettlementGenerationResult generateSettlements(const World& world,
                                               const SettlementGenerationParams& params)
{
    SettlementGenerationResult result;
    if (params.count <= 0) return result;

    std::mt19937 rng(params.seed);
    const float lo = params.margin;
    const float hiX = static_cast<float>(WORLD_W) - params.margin;
    const float hiY = static_cast<float>(WORLD_H) - params.margin;
    if (hiX <= lo || hiY <= lo) return result;

    std::uniform_real_distribution<float> distX(lo, hiX);
    std::uniform_real_distribution<float> distY(lo, hiY);

    int attempts = 0;
    while (static_cast<int>(result.settlements.size()) < params.count && attempts < params.maxAttempts) {
        ++attempts;
        const float x = distX(rng);
        const float y = distY(rng);
        if (!isGoodSettlementSpot(world, x, y)) continue;

        bool farEnough = true;
        for (const Settlement& s : result.settlements) {
            if (distance(s, Settlement{x, y, ""}) < params.minSpacing) { farEnough = false; break; }
        }
        if (!farEnough) continue;

        Settlement s;
        s.x = x;
        s.y = y;
        s.name = "Settlement " + std::to_string(result.settlements.size() + 1);
        result.settlements.push_back(std::move(s));
    }

    // Nearest-neighbor MST (Prim's): grow a connected set, always linking the
    // closest (connected, unconnected) pair. Each edge is a real GridNav path,
    // not a straight line, so roads bend around obstacles.
    const std::size_t n = result.settlements.size();
    if (n >= 2) {
        std::vector<bool> connected(n, false);
        connected[0] = true;
        std::size_t remaining = n - 1;

        while (remaining > 0) {
            float bestDist = -1.f;
            std::size_t bestFrom = 0, bestTo = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (!connected[i]) continue;
                for (std::size_t j = 0; j < n; ++j) {
                    if (connected[j]) continue;
                    const float d = distance(result.settlements[i], result.settlements[j]);
                    if (bestDist < 0.f || d < bestDist) {
                        bestDist = d;
                        bestFrom = i;
                        bestTo = j;
                    }
                }
            }

            const NavPoint start = GridNav::worldToTile(result.settlements[bestFrom].x,
                                                        result.settlements[bestFrom].y);
            const NavPoint goal = GridNav::worldToTile(result.settlements[bestTo].x,
                                                       result.settlements[bestTo].y);
            std::vector<NavPoint> path = GridNav::findPath(start.x, start.y, goal.x, goal.y, world);
            if (!path.empty()) {
                SettlementRoad road;
                road.waypoints = std::move(path);
                result.roads.push_back(std::move(road));
            }

            connected[bestTo] = true;
            --remaining;
        }
    }

    return result;
}
