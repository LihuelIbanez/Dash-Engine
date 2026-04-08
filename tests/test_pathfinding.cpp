// ═════════════════════════════════════════════════════════════════════════════
// test_pathfinding — A* pathfinding on the world grid
// ═════════════════════════════════════════════════════════════════════════════
#include "GridNav.h"
#include "World.h"
#include <cstdio>
#include <cmath>

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

// ── Test: trivial path (same tile) ───────────────────────────────────────────
static void test_same_tile()
{
    std::printf("  test_same_tile\n");

    World w;
    w.generate(42);

    auto path = GridNav::findPath(10, 10, 10, 10, w);
    ASSERT(!path.empty(), "path to self should not be empty");
    ASSERT(path.size() == 1, "path to self has 1 waypoint");
    if (!path.empty()) {
        ASSERT(path[0].x == 10 && path[0].y == 10, "waypoint is the start/goal");
    }
}

// ── Test: straight path on walkable terrain ──────────────────────────────────
static void test_straight_path()
{
    std::printf("  test_straight_path\n");

    World w;
    // Make a fully walkable world
    for (int y = 0; y < WORLD_H; ++y)
        for (int x = 0; x < WORLD_W; ++x) {
            w.grid[y][x].type = TileType::Grass;
            w.grid[y][x].walkable = true;
        }

    auto path = GridNav::findPath(5, 5, 15, 5, w);
    ASSERT(!path.empty(), "path found on open terrain");
    ASSERT(path.front().x == 5 && path.front().y == 5, "path starts at origin");
    ASSERT(path.back().x == 15 && path.back().y == 5, "path ends at goal");
}

// ── Test: path around obstacle ───────────────────────────────────────────────
static void test_path_around_obstacle()
{
    std::printf("  test_path_around_obstacle\n");

    World w;
    for (int y = 0; y < WORLD_H; ++y)
        for (int x = 0; x < WORLD_W; ++x) {
            w.grid[y][x].type = TileType::Grass;
            w.grid[y][x].walkable = true;
        }

    // Place a wall from (10,3) to (10,7)
    for (int y = 3; y <= 7; ++y) {
        w.grid[y][10].type = TileType::Mountain;
        w.grid[y][10].walkable = false;
    }

    auto path = GridNav::findPath(8, 5, 12, 5, w);
    ASSERT(!path.empty(), "path found around obstacle");

    // Verify path doesn't cross the wall
    bool crossesWall = false;
    for (auto& p : path) {
        if (p.x == 10 && p.y >= 3 && p.y <= 7) {
            crossesWall = true;
            break;
        }
    }
    ASSERT(!crossesWall, "path does not cross wall");
    ASSERT(path.back().x == 12 && path.back().y == 5, "path reaches goal");
}

// ── Test: no path to unreachable goal ────────────────────────────────────────
static void test_unreachable_goal()
{
    std::printf("  test_unreachable_goal\n");

    World w;
    for (int y = 0; y < WORLD_H; ++y)
        for (int x = 0; x < WORLD_W; ++x) {
            w.grid[y][x].type = TileType::Grass;
            w.grid[y][x].walkable = true;
        }

    // Make goal tile unwalkable
    w.grid[20][20].walkable = false;

    auto path = GridNav::findPath(5, 5, 20, 20, w);
    ASSERT(path.empty(), "no path to unwalkable goal");
}

// ── Test: worldToTile clamping ───────────────────────────────────────────────
static void test_world_to_tile_clamping()
{
    std::printf("  test_world_to_tile_clamping\n");

    auto p1 = GridNav::worldToTile(-5.0f, -3.0f);
    ASSERT(p1.x == 0 && p1.y == 0, "negative coords clamped to 0,0");

    auto p2 = GridNav::worldToTile(999.f, 999.f);
    ASSERT(p2.x == WORLD_W - 1, "large x clamped");
    ASSERT(p2.y == WORLD_H - 1, "large y clamped");
}

// ── Test: tileToCentre ───────────────────────────────────────────────────────
static void test_tile_to_centre()
{
    std::printf("  test_tile_to_centre\n");

    float wx, wy;
    GridNav::tileToCentre(5, 10, wx, wy);
    ASSERT(std::fabs(wx - 5.5f) < 0.01f, "centre x = tile + 0.5");
    ASSERT(std::fabs(wy - 10.5f) < 0.01f, "centre y = tile + 0.5");
}

// ── Test: diagonal path on open terrain ──────────────────────────────────────
static void test_diagonal_path()
{
    std::printf("  test_diagonal_path\n");

    World w;
    for (int y = 0; y < WORLD_H; ++y)
        for (int x = 0; x < WORLD_W; ++x) {
            w.grid[y][x].type = TileType::Grass;
            w.grid[y][x].walkable = true;
        }

    auto path = GridNav::findPath(0, 0, 10, 10, w);
    ASSERT(!path.empty(), "diagonal path found");
    ASSERT(path.front().x == 0 && path.front().y == 0, "starts at origin");
    ASSERT(path.back().x == 10 && path.back().y == 10, "reaches goal");
    // Diagonal should be shorter than Manhattan distance
    ASSERT(path.size() <= 12, "diagonal path is reasonably short");
}

int main()
{
    std::printf("=== test_pathfinding ===\n");

    test_same_tile();
    test_straight_path();
    test_path_around_obstacle();
    test_unreachable_goal();
    test_world_to_tile_clamping();
    test_tile_to_centre();
    test_diagonal_path();

    std::printf("\n  Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
