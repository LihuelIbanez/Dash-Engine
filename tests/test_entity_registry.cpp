// ═════════════════════════════════════════════════════════════════════════════
// test_entity_registry — EntityRegistry operations + SceneData v1→v2 migration
// ═════════════════════════════════════════════════════════════════════════════
#include "EntityRegistry.h"
#include "ComponentSerialization.h"
#include "SceneData.h"
#include "World.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

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

static const char* kTempFile = "/tmp/dash_test_registry_scene.json";

// ── Test: createEntity + addComponent + getComponent ─────────────────────────
static void test_create_and_add_components()
{
    std::printf("  test_create_and_add_components\n");

    EntityRegistry reg;
    uint64_t id = reg.createEntity();
    ASSERT(id > 0, "entity ID is valid");
    ASSERT_EQ(reg.allEntities().size(), (size_t)1, "one entity registered");

    auto& tf = reg.addComponent<TransformComponent>(id);
    tf.x = 5.f; tf.y = 10.f;

    auto& hp = reg.addComponent<HealthComponent>(id);
    hp.health    = 80;
    hp.maxHealth = 100;

    auto* gotTf = reg.getComponent<TransformComponent>(id);
    ASSERT(gotTf != nullptr, "TransformComponent found");
    ASSERT_FEQ(gotTf->x, 5.f,  "transform.x is 5");
    ASSERT_FEQ(gotTf->y, 10.f, "transform.y is 10");

    auto* gotHp = reg.getComponent<HealthComponent>(id);
    ASSERT(gotHp != nullptr, "HealthComponent found");
    ASSERT_EQ(gotHp->health, 80, "health is 80");

    ASSERT(reg.hasComponent<TransformComponent>(id), "hasComponent Transform");
    ASSERT(reg.hasComponent<HealthComponent>(id),    "hasComponent Health");
    ASSERT(!reg.hasComponent<ManaComponent>(id),     "no ManaComponent yet");
}

// ── Test: getComponent returns nullptr when component absent ──────────────────
static void test_getcomponent_nullptr()
{
    std::printf("  test_getcomponent_nullptr\n");

    EntityRegistry reg;
    uint64_t id = reg.createEntity();

    ASSERT(reg.getComponent<TransformComponent>(id) == nullptr, "nullptr for missing component");
    ASSERT(reg.getComponent<StatsComponent>(9999)   == nullptr, "nullptr for missing entity");
}

// ── Test: addComponent is idempotent (no duplicate) ───────────────────────────
static void test_add_component_idempotent()
{
    std::printf("  test_add_component_idempotent\n");

    EntityRegistry reg;
    uint64_t id = reg.createEntity();

    auto& tf1 = reg.addComponent<TransformComponent>(id);
    tf1.x = 7.f;
    auto& tf2 = reg.addComponent<TransformComponent>(id); // second call
    (void)tf2;

    ASSERT_EQ(reg.getComponents(id).size(), (size_t)1, "only one TransformComponent");
}

// ── Test: removeComponent ─────────────────────────────────────────────────────
static void test_remove_component()
{
    std::printf("  test_remove_component\n");

    EntityRegistry reg;
    uint64_t id = reg.createEntity();
    reg.addComponent<HealthComponent>(id);
    reg.addComponent<ManaComponent>(id);

    ASSERT(reg.hasComponent<HealthComponent>(id), "has Health before remove");
    reg.removeComponent<HealthComponent>(id);
    ASSERT(!reg.hasComponent<HealthComponent>(id), "Health gone after remove");
    ASSERT(reg.hasComponent<ManaComponent>(id),    "Mana still present");
}

// ── Test: destroyEntity removes all components ────────────────────────────────
static void test_destroy_entity()
{
    std::printf("  test_destroy_entity\n");

    EntityRegistry reg;
    uint64_t id = reg.createEntity();
    reg.addComponent<TransformComponent>(id);
    reg.addComponent<HealthComponent>(id);

    reg.destroyEntity(id);

    ASSERT_EQ(reg.allEntities().size(), (size_t)0, "no entities after destroy");
    ASSERT(reg.getComponent<TransformComponent>(id) == nullptr, "no Transform after destroy");
}

// ── Test: clear resets everything ─────────────────────────────────────────────
static void test_clear()
{
    std::printf("  test_clear\n");

    EntityRegistry reg;
    reg.createEntity();
    reg.createEntity();
    ASSERT_EQ(reg.allEntities().size(), (size_t)2, "two entities");

    reg.clear();
    ASSERT_EQ(reg.allEntities().size(), (size_t)0, "cleared");

    uint64_t newId = reg.createEntity();
    ASSERT_EQ(newId, (uint64_t)1, "ID restarted at 1 after clear");
}

// ── Test: multiple entities ───────────────────────────────────────────────────
static void test_multiple_entities()
{
    std::printf("  test_multiple_entities\n");

    EntityRegistry reg;
    uint64_t a = reg.createEntity();
    uint64_t b = reg.createEntity();
    uint64_t c = reg.createEntity();

    reg.addComponent<TransformComponent>(a).x = 1.f;
    reg.addComponent<TransformComponent>(b).x = 2.f;
    reg.addComponent<TransformComponent>(c).x = 3.f;
    reg.addComponent<StatsComponent>(b).level = 5;

    ASSERT_FEQ(reg.getComponent<TransformComponent>(a)->x, 1.f, "a.x");
    ASSERT_FEQ(reg.getComponent<TransformComponent>(b)->x, 2.f, "b.x");
    ASSERT_FEQ(reg.getComponent<TransformComponent>(c)->x, 3.f, "c.x");
    ASSERT_EQ(reg.getComponent<StatsComponent>(b)->level, 5, "b stats level");
    ASSERT(reg.getComponent<StatsComponent>(a) == nullptr, "a has no Stats");
    ASSERT_EQ(reg.allEntities().size(), (size_t)3, "three entities");
}

// ── Test: SceneData v2 serialize + deserialize component roundtrip ────────────
static void test_scene_components_roundtrip()
{
    std::printf("  test_scene_components_roundtrip\n");

    SceneData scene;
    scene.createDefault();

    // Manually populate components on the player entity
    EntityData& player = scene.entities[0];
    player.components.clear();
    player.components.push_back(TransformComponent{7.f, 13.f});
    player.components.push_back(HealthComponent{75, 120});
    player.components.push_back(StatsComponent{});
    std::get<StatsComponent>(player.components.back()).level = 3;

    // Add an enemy with components
    EntityData enemy;
    enemy.id   = scene.allocateEntityId();
    enemy.type = EntityData::Type::Enemy;
    enemy.name = "Skeleton";
    enemy.x    = 20.f;
    enemy.y    = 25.f;
    enemy.components.push_back(TransformComponent{20.f, 25.f});
    enemy.components.push_back(HealthComponent{50, 50});
    enemy.components.push_back(AIComponent{AIComponent::Behavior::Patrol, 6.f, 4.f});
    scene.entities.push_back(enemy);

    ASSERT(scene.saveToFile(kTempFile), "save with components succeeds");

    SceneData loaded;
    ASSERT(loaded.loadFromFile(kTempFile), "load back succeeds");
    ASSERT_EQ(loaded.sceneVersion, SceneData::kCurrentVersion, "loaded version matches current");
    ASSERT_EQ(loaded.entities.size(), (size_t)2, "two entities loaded");

    // Player components
    auto& lp = loaded.entities[0];
    ASSERT(!lp.components.empty(), "player has components");
    bool hasTf = false, hasHp = false, hasSt = false;
    for (auto& c : lp.components) {
        if (auto* tf = std::get_if<TransformComponent>(&c)) {
            ASSERT_FEQ(tf->x, 7.f,  "player transform.x");
            ASSERT_FEQ(tf->y, 13.f, "player transform.y");
            hasTf = true;
        }
        if (auto* hp = std::get_if<HealthComponent>(&c)) {
            ASSERT_EQ(hp->health, 75,   "player health");
            ASSERT_EQ(hp->maxHealth, 120, "player maxHealth");
            hasHp = true;
        }
        if (auto* st = std::get_if<StatsComponent>(&c)) {
            ASSERT_EQ(st->level, 3, "player stats level");
            hasSt = true;
        }
    }
    ASSERT(hasTf, "player has TransformComponent");
    ASSERT(hasHp, "player has HealthComponent");
    ASSERT(hasSt, "player has StatsComponent");

    // Enemy AIComponent
    auto& le = loaded.entities[1];
    bool hasAI = false;
    for (auto& c : le.components) {
        if (auto* ai = std::get_if<AIComponent>(&c)) {
            ASSERT(ai->behavior == AIComponent::Behavior::Patrol, "enemy AI behavior");
            ASSERT_FEQ(ai->detectionRange, 6.f, "detectionRange");
            hasAI = true;
        }
    }
    ASSERT(hasAI, "enemy has AIComponent");
}

// ── Test: load legacy v1 scene → migration populates components ────────────────
static void test_legacy_v1_migration()
{
    std::printf("  test_legacy_v1_migration\n");

    // Write a minimal v1 JSON scene directly (no components, no sceneVersion)
    const char* v1Json = R"({
  "name": "LegacyScene",
  "worldSeed": 99999,
  "nextEntityId": 3,
  "entities": [
    {"id": 1, "type": "Player", "name": "OldHero", "x": 32.0, "y": 32.0, "class": "Warrior"},
    {"id": 2, "type": "Enemy",  "name": "OldSkeleton", "x": 15.0, "y": 15.0}
  ]
})";

    {
        std::ofstream f(kTempFile);
        f << v1Json;
    }

    SceneData loaded;
    ASSERT(loaded.loadFromFile(kTempFile), "load v1 scene succeeds");
    ASSERT_EQ(loaded.entities.size(), (size_t)2, "two entities loaded");

    // Player should have been migrated
    auto& player = loaded.entities[0];
    ASSERT(!player.components.empty(), "player has migrated components");
    bool hasTf = false, hasHp = false;
    for (auto& c : player.components) {
        if (auto* tf = std::get_if<TransformComponent>(&c)) {
            ASSERT_FEQ(tf->x, 32.f, "player transform.x from migration");
            hasTf = true;
        }
        if (std::holds_alternative<HealthComponent>(c))
            hasHp = true;
    }
    ASSERT(hasTf, "player has TransformComponent after migration");
    ASSERT(hasHp, "player has HealthComponent after migration");

    // Enemy should have AI component from migration
    auto& enemy = loaded.entities[1];
    bool hasAI = false;
    for (auto& c : enemy.components)
        if (std::holds_alternative<AIComponent>(c))
            hasAI = true;
    ASSERT(hasAI, "enemy has AIComponent after v1 migration");
}

// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("=== test_entity_registry ===\n");

    test_create_and_add_components();
    test_getcomponent_nullptr();
    test_add_component_idempotent();
    test_remove_component();
    test_destroy_entity();
    test_clear();
    test_multiple_entities();
    test_scene_components_roundtrip();
    test_legacy_v1_migration();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
