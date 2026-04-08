// ═════════════════════════════════════════════════════════════════════════════
// test_move_edit_commands — verify MoveEntityCommand and EditPropertyCommand
// ═════════════════════════════════════════════════════════════════════════════
#include "SceneData.h"
#include "World.h"
#include "CommandStack.h"
#include "MoveEntityCommand.h"
#include "EditPropertyCommand.h"
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

#define ASSERT_EQ(a, b, msg)   ASSERT((a) == (b), msg)
#define ASSERT_FEQ(a, b, msg)  ASSERT(std::fabs((a)-(b)) < 0.001f, msg)
#define ASSERT_STR(a, b, msg)  ASSERT(std::string(a) == std::string(b), msg)

// ── Helper ────────────────────────────────────────────────────────────────────
static SceneData makeScene()
{
    SceneData s;
    s.createDefault();
    EntityData e;
    e.id   = s.allocateEntityId();
    e.type = EntityData::Type::Enemy;
    e.name = "TestEnemy";
    e.x    = 10.f;
    e.y    = 15.f;
    s.entities.push_back(e);
    return s;
}

// ── Test: MoveEntityCommand apply/undo/redo ───────────────────────────────────
static void test_move_apply_undo_redo()
{
    std::printf("  test_move_apply_undo_redo\n");

    SceneData scene = makeScene();
    World world;
    world.generate(scene.worldSeed);
    CommandStack stack;

    uint64_t id = scene.entities[0].id;

    auto cmd = std::make_unique<MoveEntityCommand>(id, 10.f, 15.f, 20.f, 25.f);
    ASSERT_STR(cmd->name(), "Move Entity", "command name");

    stack.execute(std::move(cmd), scene, world);
    ASSERT_FEQ(scene.entities[0].x, 20.f, "x after apply");
    ASSERT_FEQ(scene.entities[0].y, 25.f, "y after apply");

    stack.undo(scene, world);
    ASSERT_FEQ(scene.entities[0].x, 10.f, "x after undo");
    ASSERT_FEQ(scene.entities[0].y, 15.f, "y after undo");

    stack.redo(scene, world);
    ASSERT_FEQ(scene.entities[0].x, 20.f, "x after redo");
    ASSERT_FEQ(scene.entities[0].y, 25.f, "y after redo");
}

// ── Test: consecutive moves — undo each step ─────────────────────────────────
static void test_move_chain()
{
    std::printf("  test_move_chain\n");

    SceneData scene = makeScene();
    World world;
    world.generate(scene.worldSeed);
    CommandStack stack;

    uint64_t id = scene.entities[0].id;

    stack.execute(std::make_unique<MoveEntityCommand>(id, 10.f, 15.f, 20.f, 20.f), scene, world);
    stack.execute(std::make_unique<MoveEntityCommand>(id, 20.f, 20.f, 30.f, 30.f), scene, world);

    ASSERT_FEQ(scene.entities[0].x, 30.f, "x at step 2");

    stack.undo(scene, world);
    ASSERT_FEQ(scene.entities[0].x, 20.f, "x after undo step 2");

    stack.undo(scene, world);
    ASSERT_FEQ(scene.entities[0].x, 10.f, "x after undo step 1");
}

// ── Test: EditPropertyCommand — Name ─────────────────────────────────────────
static void test_edit_name()
{
    std::printf("  test_edit_name\n");

    SceneData scene = makeScene();
    World world;
    world.generate(scene.worldSeed);
    CommandStack stack;

    uint64_t id = scene.entities[0].id;

    auto cmd = std::make_unique<EditPropertyCommand>(
        id, PropertyTarget::Name,
        PropertyValue{std::string("TestEnemy")},
        PropertyValue{std::string("BossEnemy")});
    ASSERT_STR(cmd->name(), "Edit Name", "command name");

    stack.execute(std::move(cmd), scene, world);
    ASSERT_STR(scene.entities[0].name, "BossEnemy", "name after apply");

    stack.undo(scene, world);
    ASSERT_STR(scene.entities[0].name, "TestEnemy", "name after undo");

    stack.redo(scene, world);
    ASSERT_STR(scene.entities[0].name, "BossEnemy", "name after redo");
}

// ── Test: EditPropertyCommand — CharClass ─────────────────────────────────────
static void test_edit_class()
{
    std::printf("  test_edit_class\n");

    SceneData scene;
    scene.createDefault();
    // add player entity
    EntityData p;
    p.id       = scene.allocateEntityId();
    p.type     = EntityData::Type::Player;
    p.name     = "Hero";
    p.charClass = "Warrior";
    p.x = 5.f; p.y = 5.f;
    scene.entities.push_back(p);

    World world;
    world.generate(scene.worldSeed);
    CommandStack stack;

    uint64_t id = scene.entities[0].id;

    stack.execute(std::make_unique<EditPropertyCommand>(
        id, PropertyTarget::CharClass,
        PropertyValue{std::string("Warrior")},
        PropertyValue{std::string("Mage")}), scene, world);

    ASSERT_STR(scene.entities[0].charClass, "Mage", "class after apply");

    stack.undo(scene, world);
    ASSERT_STR(scene.entities[0].charClass, "Warrior", "class after undo");
}

// ── Test: EditPropertyCommand — PosX / PosY ──────────────────────────────────
static void test_edit_position()
{
    std::printf("  test_edit_position\n");

    SceneData scene = makeScene();
    World world;
    world.generate(scene.worldSeed);
    CommandStack stack;

    uint64_t id = scene.entities[0].id;

    stack.execute(std::make_unique<EditPropertyCommand>(
        id, PropertyTarget::PosX,
        PropertyValue{10.f}, PropertyValue{42.f}), scene, world);

    ASSERT_FEQ(scene.entities[0].x, 42.f, "x after apply");

    stack.undo(scene, world);
    ASSERT_FEQ(scene.entities[0].x, 10.f, "x after undo");

    stack.execute(std::make_unique<EditPropertyCommand>(
        id, PropertyTarget::PosY,
        PropertyValue{15.f}, PropertyValue{7.f}), scene, world);

    ASSERT_FEQ(scene.entities[0].y, 7.f, "y after apply");

    stack.undo(scene, world);
    ASSERT_FEQ(scene.entities[0].y, 15.f, "y after undo");
}

// ── Test: redo cleared on new command ────────────────────────────────────────
static void test_redo_cleared()
{
    std::printf("  test_redo_cleared\n");

    SceneData scene = makeScene();
    World world;
    world.generate(scene.worldSeed);
    CommandStack stack;

    uint64_t id = scene.entities[0].id;

    stack.execute(std::make_unique<MoveEntityCommand>(id, 10.f, 15.f, 20.f, 20.f), scene, world);
    stack.undo(scene, world);
    ASSERT(stack.canRedo(), "can redo before new cmd");

    stack.execute(std::make_unique<MoveEntityCommand>(id, 10.f, 15.f, 5.f, 5.f), scene, world);
    ASSERT(!stack.canRedo(), "redo cleared after new cmd");
}

// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("=== test_move_edit_commands ===\n");

    test_move_apply_undo_redo();
    test_move_chain();
    test_edit_name();
    test_edit_class();
    test_edit_position();
    test_redo_cleared();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
