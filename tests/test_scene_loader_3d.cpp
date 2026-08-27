// ═════════════════════════════════════════════════════════════════════════════
// test_scene_loader_3d — Sprint 15: scene components → RenderInstance mapping
// ═════════════════════════════════════════════════════════════════════════════
#include "rendering/vulkan/SceneLoader.h"
#include "SceneData.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using dash::vkexp::SceneLoader;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

#define ASSERT_EQ(a, b, msg)  ASSERT((a) == (b), msg)
#define ASSERT_FEQ(a, b, msg) ASSERT(std::fabs((a)-(b)) < 0.001f, msg)

static const char* kTempScene = "/tmp/dash_test_scene_loader_3d.json";

// Base offsets/scales SceneLoader applies per entity type.
static constexpr float kPlayerBaseHeight = 1.0f;
static constexpr float kEnemyBaseHeight  = 0.6f;

// ── Test: TransformComponent drives position, rotation and scale ─────────────
static void test_transform_component_drives_instance()
{
    std::printf("  test_transform_component_drives_instance\n");

    SceneData scene;
    scene.createDefault();

    EntityData& player = scene.entities[0];
    player.components.clear();
    TransformComponent tf;
    tf.x = 10.f; tf.y = 20.f; tf.z = 0.5f;
    tf.yawDeg = 45.f; tf.pitchDeg = 10.f; tf.rollDeg = -5.f;
    tf.scale = 2.f;
    player.components.push_back(tf);

    ASSERT(scene.saveToFile(kTempScene), "scene saved");

    const auto loaded = SceneLoader::load(kTempScene);
    ASSERT(loaded.valid, "scene loaded");

    const auto instances = SceneLoader::loadInstances(loaded);
    ASSERT_EQ(instances.size(), (size_t)1, "one instance");
    if (instances.empty()) return;

    const auto& inst = instances[0];
    ASSERT_FEQ(inst.position.x, 10.f, "position.x from transform");
    ASSERT_FEQ(inst.position.z, 20.f, "scene y maps to world z");
    ASSERT_FEQ(inst.position.y, kPlayerBaseHeight + 0.5f, "transform z adds to base height");
    ASSERT_FEQ(inst.yawDeg, 45.f,  "yaw propagated");
    ASSERT_FEQ(inst.pitchDeg, 10.f, "pitch propagated");
    ASSERT_FEQ(inst.rollDeg, -5.f,  "roll propagated");
    ASSERT_FEQ(inst.scale.x, 0.26f * 2.f, "uniform scale multiplies base scale");
    ASSERT(inst.isPlayer, "player flagged");
}

// ── Test: RenderComponent drives mesh, material, visibility and layer ────────
static void test_render_component_drives_instance()
{
    std::printf("  test_render_component_drives_instance\n");

    SceneData scene;
    scene.createDefault();

    EntityData& player = scene.entities[0];
    player.components.clear();
    player.components.push_back(TransformComponent{5.f, 5.f});
    RenderComponent rc;
    rc.mesh = "models/hero.gltf";
    rc.material = "materials/hero.mat";
    rc.visible = false;
    rc.layer = 3;
    rc.renderMode = static_cast<int>(RenderMode::BillboardSprite);
    player.components.push_back(rc);

    ASSERT(scene.saveToFile(kTempScene), "scene saved");

    const auto loaded = SceneLoader::load(kTempScene);
    const auto instances = SceneLoader::loadInstances(loaded);
    ASSERT_EQ(instances.size(), (size_t)1, "one instance");
    if (instances.empty()) return;

    const auto& inst = instances[0];
    ASSERT(inst.meshId == "models/hero.gltf", "mesh id propagated");
    ASSERT(inst.materialId == "materials/hero.mat", "material id propagated");
    ASSERT(!inst.visible, "visible=false propagated");
    ASSERT_EQ(inst.layer, 3, "layer propagated");
    ASSERT_EQ(inst.renderMode, static_cast<int>(RenderMode::BillboardSprite), "render mode propagated");
}

// ── Test: entity without components keeps the legacy defaults ────────────────
static void test_legacy_entity_defaults()
{
    std::printf("  test_legacy_entity_defaults\n");

    SceneData scene;
    scene.createDefault();

    EntityData enemy;
    enemy.id   = scene.allocateEntityId();
    enemy.type = EntityData::Type::Enemy;
    enemy.name = "Skeleton";
    enemy.x    = 12.f;
    enemy.y    = 14.f;
    scene.entities.push_back(enemy);

    ASSERT(scene.saveToFile(kTempScene), "scene saved");

    const auto loaded = SceneLoader::load(kTempScene);
    const auto instances = SceneLoader::loadInstances(loaded);
    ASSERT_EQ(instances.size(), (size_t)2, "player + enemy");
    if (instances.size() < 2) return;

    const auto& e = instances[1];
    ASSERT_FEQ(e.position.x, 12.f, "enemy x");
    ASSERT_FEQ(e.position.z, 14.f, "enemy z");
    ASSERT_FEQ(e.position.y, kEnemyBaseHeight, "enemy sits at base height");
    ASSERT(!e.isPlayer, "enemy not flagged as player");
    ASSERT(e.visible, "visible by default");
    ASSERT(e.meshId == "cube", "defaults to builtin cube mesh");
}

// ── Test: player position comes from the TransformComponent ─────────────────
static void test_player_position_uses_transform()
{
    std::printf("  test_player_position_uses_transform\n");

    SceneData scene;
    scene.createDefault();
    scene.entities[0].components.clear();
    scene.entities[0].components.push_back(TransformComponent{7.f, 9.f});

    ASSERT(scene.saveToFile(kTempScene), "scene saved");

    const auto loaded = SceneLoader::load(kTempScene);
    float px = 0.f, pz = 0.f;
    ASSERT(SceneLoader::loadPlayerPosition(loaded, px, pz), "player found");
    ASSERT_FEQ(px, 7.f, "player x from transform");
    ASSERT_FEQ(pz, 9.f, "player z from transform");
}

// ── Test: missing file degrades gracefully ──────────────────────────────────
static void test_missing_file()
{
    std::printf("  test_missing_file\n");

    const auto loaded = SceneLoader::load("/tmp/dash_does_not_exist_12345.json");
    ASSERT(!loaded.valid, "missing scene is not valid");
    ASSERT(SceneLoader::loadInstances(loaded).empty(), "no instances from invalid scene");

    float px = 0.f, pz = 0.f;
    ASSERT(!SceneLoader::loadPlayerPosition(loaded, px, pz), "no player from invalid scene");
}

// ── Test: distinct meshes survive the scene round-trip ──────────────────────
static void test_multiple_meshes_preserved()
{
    std::printf("  test_multiple_meshes_preserved\n");

    SceneData scene;
    scene.createDefault();
    scene.entities[0].components.clear();
    scene.entities[0].components.push_back(TransformComponent{1.f, 1.f});

    const char* meshes[] = {"wolf.gltf", "cube", "wolf.gltf"};
    for (int i = 0; i < 3; ++i) {
        EntityData e;
        e.id   = scene.allocateEntityId();
        e.type = EntityData::Type::Enemy;
        e.name = "E" + std::to_string(i);
        e.x    = static_cast<float>(10 + i);
        e.y    = 10.f;
        e.components.push_back(TransformComponent{e.x, e.y});
        RenderComponent rc;
        rc.mesh = meshes[i];
        e.components.push_back(rc);
        scene.entities.push_back(e);
    }

    ASSERT(scene.saveToFile(kTempScene), "scene saved");

    const auto loaded = SceneLoader::load(kTempScene);
    const auto instances = SceneLoader::loadInstances(loaded);
    ASSERT_EQ(instances.size(), (size_t)4, "player + three enemies");

    int wolfCount = 0, cubeCount = 0;
    for (const auto& inst : instances) {
        if (inst.meshId == "wolf.gltf") ++wolfCount;
        else if (inst.meshId == "cube") ++cubeCount;
    }
    ASSERT_EQ(wolfCount, 2, "two instances reference the same custom mesh");
    ASSERT_EQ(cubeCount, 2, "player default + explicit cube");
}

// ── Test: only entities with PhysicsComponent produce bodies ────────────────
static void test_physics_bodies_from_components()
{
    std::printf("  test_physics_bodies_from_components\n");

    SceneData scene;
    scene.createDefault();
    // Player: transform only, no physics -> must NOT spawn a body.
    scene.entities[0].components.clear();
    scene.entities[0].components.push_back(TransformComponent{1.f, 2.f});

    EntityData dynamic;
    dynamic.id   = scene.allocateEntityId();
    dynamic.type = EntityData::Type::Enemy;
    dynamic.name = "Crate";
    dynamic.x = 10.f; dynamic.y = 12.f;
    TransformComponent dtf; dtf.x = 10.f; dtf.y = 12.f; dtf.z = 2.f;
    dynamic.components.push_back(dtf);
    PhysicsComponent dp;
    dp.halfExtentX = 0.5f; dp.halfExtentY = 0.6f; dp.halfExtentZ = 0.7f;
    dp.mass = 3.f; dp.isStatic = false;
    dynamic.components.push_back(dp);
    scene.entities.push_back(dynamic);

    EntityData staticBody;
    staticBody.id   = scene.allocateEntityId();
    staticBody.type = EntityData::Type::Enemy;
    staticBody.name = "Wall";
    staticBody.x = 20.f; staticBody.y = 20.f;
    staticBody.components.push_back(TransformComponent{20.f, 20.f});
    PhysicsComponent sp; sp.isStatic = true;
    staticBody.components.push_back(sp);
    scene.entities.push_back(staticBody);

    ASSERT(scene.saveToFile(kTempScene), "scene saved");

    const auto loaded = SceneLoader::load(kTempScene);
    const auto bodies = SceneLoader::loadPhysicsBodies(loaded);
    ASSERT_EQ(bodies.size(), (size_t)2, "only entities with PhysicsComponent spawn bodies");
    if (bodies.size() < 2) return;

    const auto& b0 = bodies[0];
    ASSERT_EQ(b0.entityId, dynamic.id, "body maps back to its entity");
    ASSERT_FEQ(b0.position.x, 10.f, "body x from transform");
    ASSERT_FEQ(b0.position.z, 12.f, "body z from transform y");
    ASSERT_FEQ(b0.position.y, kEnemyBaseHeight + 2.f, "body height includes transform z");
    ASSERT_FEQ(b0.halfExtents.y, 0.6f, "half extents preserved");
    ASSERT_FEQ(b0.mass, 3.f, "mass preserved");
    ASSERT(!b0.isStatic, "dynamic flag preserved");

    ASSERT(bodies[1].isStatic, "static flag preserved");
}

// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("=== test_scene_loader_3d ===\n");

    test_transform_component_drives_instance();
    test_render_component_drives_instance();
    test_legacy_entity_defaults();
    test_player_position_uses_transform();
    test_missing_file();
    test_multiple_meshes_preserved();
    test_physics_bodies_from_components();

    std::error_code ec;
    fs::remove(kTempScene, ec);

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
