// ═════════════════════════════════════════════════════════════════════════════
// test_undo_redo_commands — verify CommandStack + concrete commands
// ═════════════════════════════════════════════════════════════════════════════
#include "SceneData.h"
#include "World.h"
#include "CommandStack.h"
#include "PaintTileCommand.h"
#include "PlaceEnemyCommand.h"
#include "EraseCommand.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <memory>

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)

// ── Test: paint tile undo/redo ───────────────────────────────────────────────
static void test_paint_tile()
{
    std::printf("  test_paint_tile\n");

    SceneData scene;
    scene.createDefault();
    World world;
    world.generate(scene.worldSeed);

    CommandStack stack;

    TileType origType = world.grid[5][5].type;
    TileType newType  = (origType == TileType::Stone) ? TileType::Sand : TileType::Stone;

    auto cmd = std::make_unique<PaintTileCommand>(5, 5, newType);
    stack.execute(std::move(cmd), scene, world);

    ASSERT_EQ((int)world.grid[5][5].type, (int)newType, "tile painted");
    ASSERT(stack.canUndo(), "can undo after paint");

    stack.undo(scene, world);
    ASSERT_EQ((int)world.grid[5][5].type, (int)origType, "tile restored after undo");

    stack.redo(scene, world);
    ASSERT_EQ((int)world.grid[5][5].type, (int)newType, "tile repainted after redo");
}

// ── Test: place + erase entity undo/redo ─────────────────────────────────────
static void test_place_erase_entity()
{
    std::printf("  test_place_erase_entity\n");

    SceneData scene;
    scene.createDefault();
    World world;
    world.generate(scene.worldSeed);
    CommandStack stack;

    size_t origCount = scene.entities.size();

    // Place enemy
    uint64_t newId = scene.allocateEntityId();
    auto placeCmd = std::make_unique<PlaceEnemyCommand>(10.f, 10.f, newId, "TestEnemy");
    stack.execute(std::move(placeCmd), scene, world);
    ASSERT_EQ(scene.entities.size(), origCount + 1, "entity added");

    // Undo place
    stack.undo(scene, world);
    ASSERT_EQ(scene.entities.size(), origCount, "entity removed by undo");

    // Redo place
    stack.redo(scene, world);
    ASSERT_EQ(scene.entities.size(), origCount + 1, "entity re-added by redo");

    // Erase the entity
    auto eraseCmd = std::make_unique<EraseCommand>(newId);
    stack.execute(std::move(eraseCmd), scene, world);
    ASSERT_EQ(scene.entities.size(), origCount, "entity erased");

    // Undo erase (should restore)
    stack.undo(scene, world);
    ASSERT_EQ(scene.entities.size(), origCount + 1, "entity restored by undo erase");
}

// ── Test: stack clear ────────────────────────────────────────────────────────
static void test_stack_clear()
{
    std::printf("  test_stack_clear\n");

    SceneData scene;
    scene.createDefault();
    World world;
    world.generate(scene.worldSeed);
    CommandStack stack;

    auto cmd = std::make_unique<PaintTileCommand>(1, 1, TileType::Snow);
    stack.execute(std::move(cmd), scene, world);

    ASSERT(stack.canUndo(), "can undo before clear");
    stack.clear();
    ASSERT(!stack.canUndo(), "cannot undo after clear");
    ASSERT(!stack.canRedo(), "cannot redo after clear");
}

// ── Test: redo cleared after new command ──────────────────────────────────────
static void test_redo_cleared_after_new_command()
{
    std::printf("  test_redo_cleared_after_new_command\n");

    SceneData scene;
    scene.createDefault();
    World world;
    world.generate(scene.worldSeed);
    CommandStack stack;

    auto cmd1 = std::make_unique<PaintTileCommand>(2, 2, TileType::Sand);
    stack.execute(std::move(cmd1), scene, world);
    stack.undo(scene, world);
    ASSERT(stack.canRedo(), "can redo after undo");

    // New command should clear redo stack
    auto cmd2 = std::make_unique<PaintTileCommand>(3, 3, TileType::Forest);
    stack.execute(std::move(cmd2), scene, world);
    ASSERT(!stack.canRedo(), "redo cleared after new command");
}

int main()
{
    std::printf("=== test_undo_redo_commands ===\n");

    test_paint_tile();
    test_place_erase_entity();
    test_stack_clear();
    test_redo_cleared_after_new_command();

    std::printf("\n  Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
