// ═════════════════════════════════════════════════════════════════════════════
// test_scene_serialization — round-trip save/load of SceneData
// ═════════════════════════════════════════════════════════════════════════════
#include "SceneData.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)
#define ASSERT_NEAR(a, b, eps, msg) ASSERT(std::fabs((a) - (b)) < (eps), msg)

static const char* kTempFile = "/tmp/dash_test_scene.json";

// ── Test: default scene round-trip ───────────────────────────────────────────
static void test_default_roundtrip()
{
    std::printf("  test_default_roundtrip\n");

    SceneData s;
    s.createDefault();
    ASSERT(s.saveToFile(kTempFile), "save should succeed");

    SceneData loaded;
    ASSERT(loaded.loadFromFile(kTempFile), "load should succeed");

    ASSERT_EQ(loaded.sceneName, s.sceneName, "sceneName matches");
    ASSERT_EQ(loaded.worldSeed, s.worldSeed, "worldSeed matches");
    ASSERT_EQ(loaded.entities.size(), s.entities.size(), "entity count matches");

    if (!loaded.entities.empty() && !s.entities.empty()) {
        auto& le = loaded.entities[0];
        auto& se = s.entities[0];
        ASSERT_EQ(le.id, se.id, "player id matches");
        ASSERT_EQ((int)le.type, (int)se.type, "player type matches");
        ASSERT_NEAR(le.x, se.x, 0.01f, "player x matches");
        ASSERT_NEAR(le.y, se.y, 0.01f, "player y matches");
        ASSERT_EQ(le.charClass, se.charClass, "player class matches");
    }
}

// ── Test: scene with tile overrides ──────────────────────────────────────────
static void test_tile_overrides_roundtrip()
{
    std::printf("  test_tile_overrides_roundtrip\n");

    SceneData s;
    s.createDefault();
    s.tileOverrides.push_back({5, 10, (int)TileType::Stone, true});
    s.tileOverrides.push_back({3, 4,  (int)TileType::Water, false});
    ASSERT(s.saveToFile(kTempFile), "save should succeed");

    SceneData loaded;
    ASSERT(loaded.loadFromFile(kTempFile), "load should succeed");
    ASSERT_EQ(loaded.tileOverrides.size(), (size_t)2, "2 tile overrides");
    ASSERT_EQ(loaded.tileOverrides[0].x, 5, "tile[0].x");
    ASSERT_EQ(loaded.tileOverrides[0].tileType, (int)TileType::Stone, "tile[0].type");
    ASSERT_EQ(loaded.tileOverrides[1].walkable, false, "tile[1].walkable");
}

// ── Test: multiple entities ──────────────────────────────────────────────────
static void test_multiple_entities()
{
    std::printf("  test_multiple_entities\n");

    SceneData s;
    s.createDefault();

    EntityData e;
    e.id   = s.allocateEntityId();
    e.type = EntityData::Type::Enemy;
    e.name = "Skeleton";
    e.x    = 10.f;
    e.y    = 20.f;
    s.entities.push_back(e);

    EntityData e2;
    e2.id   = s.allocateEntityId();
    e2.type = EntityData::Type::Enemy;
    e2.name = "Zombie";
    e2.x    = 15.f;
    e2.y    = 25.f;
    s.entities.push_back(e2);

    ASSERT(s.saveToFile(kTempFile), "save should succeed");

    SceneData loaded;
    ASSERT(loaded.loadFromFile(kTempFile), "load should succeed");
    ASSERT_EQ(loaded.entities.size(), (size_t)3, "3 entities (player + 2 enemies)");
    ASSERT_EQ(loaded.entities[1].name, std::string("Skeleton"), "enemy 1 name");
    ASSERT_EQ(loaded.entities[2].name, std::string("Zombie"), "enemy 2 name");
}

// ── Test: load invalid file ──────────────────────────────────────────────────
static void test_load_invalid()
{
    std::printf("  test_load_invalid\n");

    SceneData s;
    ASSERT(!s.loadFromFile("/tmp/nonexistent_dash_xyzzy.json"), "load nonexistent fails");
    ASSERT(!s.loadErrors.empty(), "load errors populated");
}

// ── Test: load corrupt JSON ──────────────────────────────────────────────────
static void test_load_corrupt_json()
{
    std::printf("  test_load_corrupt_json\n");

    // Write garbage
    {
        FILE* f = std::fopen(kTempFile, "w");
        std::fputs("{{{not valid json", f);
        std::fclose(f);
    }

    SceneData s;
    ASSERT(!s.loadFromFile(kTempFile), "corrupt json fails");
}

// ── Test: nextEntityId preserved ─────────────────────────────────────────────
static void test_next_entity_id()
{
    std::printf("  test_next_entity_id\n");

    SceneData s;
    s.createDefault();
    s.allocateEntityId(); // 2
    s.allocateEntityId(); // 3
    uint64_t expectedNext = s.nextEntityId;

    ASSERT(s.saveToFile(kTempFile), "save");
    SceneData loaded;
    ASSERT(loaded.loadFromFile(kTempFile), "load");
    ASSERT(loaded.nextEntityId >= expectedNext, "nextEntityId preserved");
}

int main()
{
    std::printf("=== test_scene_serialization ===\n");

    test_default_roundtrip();
    test_tile_overrides_roundtrip();
    test_multiple_entities();
    test_load_invalid();
    test_load_corrupt_json();
    test_next_entity_id();

    // Cleanup
    fs::remove(kTempFile);

    std::printf("\n  Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
