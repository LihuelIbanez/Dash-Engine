// ═════════════════════════════════════════════════════════════════════════════
// test_content_validation — D28: ContentValidator unit tests
// ═════════════════════════════════════════════════════════════════════════════
#include "ContentValidator.h"
#include "SceneData.h"
#include "World.h"
#include "AssetDatabase.h"
#include "Components.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static bool hasIssue(const std::vector<ValidationIssue>& issues,
                     ValidationIssue::Severity sev,
                     const std::string& substr = "")
{
    for (const auto& i : issues) {
        if (i.severity != sev) continue;
        if (substr.empty() || i.message.find(substr) != std::string::npos)
            return true;
    }
    return false;
}

static EntityData makeEntity(uint64_t id, EntityData::Type type,
                              float x = 5.f, float y = 5.f,
                              const std::string& name = "E")
{
    EntityData e;
    e.id   = id;
    e.type = type;
    e.name = name;
    e.x    = x;
    e.y    = y;
    e.components.push_back(TransformComponent{x, y});
    if (type == EntityData::Type::Enemy) {
        e.components.push_back(HealthComponent{50, 100});
    } else {
        e.components.push_back(HealthComponent{100, 100});
    }
    return e;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: valid scene (player present, one enemy, all in bounds) — 0 errors
// ─────────────────────────────────────────────────────────────────────────────
static void test_valid_scene()
{
    std::printf("  test_valid_scene\n");

    World world;
    world.generate(42); // walkable terrain

    SceneData scene;
    scene.entities.push_back(makeEntity(1, EntityData::Type::Player, 8.f, 8.f, "Hero"));
    scene.entities.push_back(makeEntity(2, EntityData::Type::Enemy,  12.f, 12.f, "Goblin"));

    AssetDatabase db;
    ContentValidator cv;
    auto issues = cv.validate(scene, world, db);

    // Filter only errors (warnings about paths etc. are fine in a blank scene)
    int errCnt = 0;
    for (const auto& i : issues)
        if (i.severity == ValidationIssue::Severity::Error) ++errCnt;

    ASSERT_EQ(errCnt, 0, "valid scene returns 0 errors");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: entity out of bounds → Error detected
// ─────────────────────────────────────────────────────────────────────────────
static void test_out_of_bounds()
{
    std::printf("  test_out_of_bounds\n");

    World world;
    world.generate(42);

    SceneData scene;
    scene.entities.push_back(makeEntity(1, EntityData::Type::Player, 8.f, 8.f, "Hero"));
    // An enemy placed way outside the 64×64 grid
    scene.entities.push_back(makeEntity(2, EntityData::Type::Enemy, 999.f, 999.f, "OOB"));

    AssetDatabase db;
    ContentValidator cv;
    auto issues = cv.validate(scene, world, db);

    ASSERT(hasIssue(issues, ValidationIssue::Severity::Error, "out of world bounds"),
           "out-of-bounds entity flagged as Error");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: no player → Error detected
// ─────────────────────────────────────────────────────────────────────────────
static void test_no_player()
{
    std::printf("  test_no_player\n");

    World world;
    world.generate(42);

    SceneData scene;
    scene.entities.push_back(makeEntity(1, EntityData::Type::Enemy, 8.f, 8.f, "Goblin"));

    AssetDatabase db;
    ContentValidator cv;
    auto issues = cv.validate(scene, world, db);

    ASSERT(hasIssue(issues, ValidationIssue::Severity::Error, "no Player"),
           "missing player flagged as Error");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: entity with unknown prefabGuid → Warning detected
// ─────────────────────────────────────────────────────────────────────────────
static void test_invalid_prefab_guid()
{
    std::printf("  test_invalid_prefab_guid\n");

    World world;
    world.generate(42);

    SceneData scene;
    EntityData player = makeEntity(1, EntityData::Type::Player, 8.f, 8.f, "Hero");
    EntityData enemy  = makeEntity(2, EntityData::Type::Enemy,  12.f, 12.f, "Goblin");
    enemy.prefabGuid  = "non-existent-guid-000";
    scene.entities.push_back(player);
    scene.entities.push_back(enemy);

    AssetDatabase db;  // empty — GUID won't be found
    ContentValidator cv;
    auto issues = cv.validate(scene, world, db);

    ASSERT(hasIssue(issues, ValidationIssue::Severity::Warning, "prefabGuid"),
           "invalid prefabGuid flagged as Warning");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: health > maxHealth → Warning detected
// ─────────────────────────────────────────────────────────────────────────────
static void test_health_exceeds_max()
{
    std::printf("  test_health_exceeds_max\n");

    World world;
    world.generate(42);

    SceneData scene;
    EntityData player = makeEntity(1, EntityData::Type::Player, 8.f, 8.f, "Hero");

    // Add an enemy with broken health values
    EntityData enemy;
    enemy.id   = 2;
    enemy.type = EntityData::Type::Enemy;
    enemy.name = "Broken";
    enemy.x    = 12.f;
    enemy.y    = 12.f;
    enemy.components.push_back(TransformComponent{12.f, 12.f});
    enemy.components.push_back(HealthComponent{200, 100}); // health > maxHealth

    scene.entities.push_back(player);
    scene.entities.push_back(enemy);

    AssetDatabase db;
    ContentValidator cv;
    auto issues = cv.validate(scene, world, db);

    ASSERT(hasIssue(issues, ValidationIssue::Severity::Warning, "health > maxHealth"),
           "health > maxHealth flagged as Warning");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6: more than one Player → Error detected
// ─────────────────────────────────────────────────────────────────────────────
static void test_multiple_players()
{
    std::printf("  test_multiple_players\n");

    World world;
    world.generate(42);

    SceneData scene;
    scene.entities.push_back(makeEntity(1, EntityData::Type::Player, 8.f, 8.f, "Hero1"));
    scene.entities.push_back(makeEntity(2, EntityData::Type::Player, 10.f, 10.f, "Hero2"));

    AssetDatabase db;
    ContentValidator cv;
    auto issues = cv.validate(scene, world, db);

    ASSERT(hasIssue(issues, ValidationIssue::Severity::Error, "more than one Player"),
           "multiple players flagged as Error");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7: duplicate IDs → Error detected
// ─────────────────────────────────────────────────────────────────────────────
static void test_duplicate_ids()
{
    std::printf("  test_duplicate_ids\n");

    World world;
    world.generate(42);

    SceneData scene;
    EntityData p = makeEntity(1, EntityData::Type::Player, 8.f, 8.f, "Hero");
    EntityData e = makeEntity(1, EntityData::Type::Enemy,  12.f, 12.f, "Goblin"); // same ID!
    scene.entities.push_back(p);
    scene.entities.push_back(e);

    AssetDatabase db;
    ContentValidator cv;
    auto issues = cv.validate(scene, world, db);

    ASSERT(hasIssue(issues, ValidationIssue::Severity::Error, "Duplicate entity ID"),
           "duplicate entity ID flagged as Error");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8: entity missing TransformComponent → Warning detected
// ─────────────────────────────────────────────────────────────────────────────
static void test_missing_transform()
{
    std::printf("  test_missing_transform\n");

    World world;
    world.generate(42);

    SceneData scene;
    // Player with no components at all
    EntityData player;
    player.id   = 1;
    player.type = EntityData::Type::Player;
    player.name = "NoTransform";
    player.x    = 8.f;
    player.y    = 8.f;
    // (no components pushed)
    scene.entities.push_back(player);

    AssetDatabase db;
    ContentValidator cv;
    auto issues = cv.validate(scene, world, db);

    ASSERT(hasIssue(issues, ValidationIssue::Severity::Warning, "no TransformComponent"),
           "missing TransformComponent flagged as Warning");
}

// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("=== test_content_validation ===\n");

    test_valid_scene();
    test_out_of_bounds();
    test_no_player();
    test_invalid_prefab_guid();
    test_health_exceeds_max();
    test_multiple_players();
    test_duplicate_ids();
    test_missing_transform();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
