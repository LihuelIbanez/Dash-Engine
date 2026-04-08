// ═════════════════════════════════════════════════════════════════════════════
// test_world_seed_determinism — same seed must produce identical world
// ═════════════════════════════════════════════════════════════════════════════
#include "World.h"
#include <cstdio>
#include <cstring>

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

// ── Test: same seed → identical grid ─────────────────────────────────────────
static void test_same_seed_identical()
{
    std::printf("  test_same_seed_identical\n");

    World a, b;
    a.generate(42);
    b.generate(42);

    bool identical = true;
    for (int y = 0; y < WORLD_H && identical; ++y) {
        for (int x = 0; x < WORLD_W && identical; ++x) {
            if (a.grid[y][x].type != b.grid[y][x].type ||
                a.grid[y][x].walkable != b.grid[y][x].walkable) {
                std::fprintf(stderr, "    Mismatch at (%d,%d): type %d vs %d\n",
                            x, y, (int)a.grid[y][x].type, (int)b.grid[y][x].type);
                identical = false;
            }
        }
    }
    ASSERT(identical, "same seed produces identical world");
}

// ── Test: different seeds → different grid ───────────────────────────────────
static void test_different_seeds_differ()
{
    std::printf("  test_different_seeds_differ\n");

    World a, b;
    a.generate(100);
    b.generate(999);

    int differences = 0;
    for (int y = 0; y < WORLD_H; ++y)
        for (int x = 0; x < WORLD_W; ++x)
            if (a.grid[y][x].type != b.grid[y][x].type) ++differences;

    ASSERT(differences > 0, "different seeds produce different worlds");
    std::printf("    (%d tiles differ out of %d)\n", differences, WORLD_W * WORLD_H);
}

// ── Test: regeneration after different seed restores original ─────────────────
static void test_regeneration_after_different()
{
    std::printf("  test_regeneration_after_different\n");

    World w;
    w.generate(77);

    // Save first state
    Tile snapshot[WORLD_H][WORLD_W];
    std::memcpy(snapshot, w.grid, sizeof(w.grid));

    // Generate with different seed
    w.generate(9999);

    // Regenerate with original seed
    w.generate(77);

    bool identical = (std::memcmp(snapshot, w.grid, sizeof(w.grid)) == 0);
    ASSERT(identical, "re-generating with same seed restores original state");
}

// ── Test: multiple runs for consistency ───────────────────────────────────────
static void test_multiple_runs_consistent()
{
    std::printf("  test_multiple_runs_consistent\n");

    // Generate 5 times with same seed and check all are identical
    World worlds[5];
    for (int i = 0; i < 5; ++i)
        worlds[i].generate(12345);

    bool allSame = true;
    for (int i = 1; i < 5 && allSame; ++i) {
        for (int y = 0; y < WORLD_H && allSame; ++y) {
            for (int x = 0; x < WORLD_W && allSame; ++x) {
                if (worlds[0].grid[y][x].type != worlds[i].grid[y][x].type ||
                    worlds[0].grid[y][x].walkable != worlds[i].grid[y][x].walkable)
                    allSame = false;
            }
        }
    }
    ASSERT(allSame, "5 generations with same seed are all identical");
}

int main()
{
    std::printf("=== test_world_seed_determinism ===\n");

    test_same_seed_identical();
    test_different_seeds_differ();
    test_regeneration_after_different();
    test_multiple_runs_consistent();

    std::printf("\n  Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
