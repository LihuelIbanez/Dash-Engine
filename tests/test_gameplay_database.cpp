// ═════════════════════════════════════════════════════════════════════════════
// test_gameplay_database — GameplayDatabase JSON loading and lookups
// ═════════════════════════════════════════════════════════════════════════════
#include "GameplayDatabase.h"
#include <cstdio>
#include <string>

// ASSETS_DIR is provided as a compile-time define by CMakeLists.txt
#ifndef ASSETS_DIR
#  define ASSETS_DIR "."
#endif

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

// ── Test: load() with a valid assets directory returns true ───────────────────
static void test_load_valid_dir(GameplayDatabase& db)
{
    std::printf("  test_load_valid_dir\n");

    bool ok = db.load(ASSETS_DIR);
    ASSERT(ok, "load() returns true for valid assets directory");
}

// ── Test: findPlayerClass returns correct data ────────────────────────────────
static void test_find_player_class(GameplayDatabase& db)
{
    std::printf("  test_find_player_class\n");

    const PlayerClassData* warrior = db.findPlayerClass("warrior");
    ASSERT(warrior != nullptr, "findPlayerClass('warrior') not null");
    if (warrior) {
        ASSERT(warrior->maxHp  > 0,  "warrior maxHp > 0");
        ASSERT(warrior->attack > 0,  "warrior attack > 0");
        ASSERT(!warrior->id.empty(), "warrior id not empty");
    }
}

// ── Test: findEnemy returns correct data ─────────────────────────────────────
static void test_find_enemy(GameplayDatabase& db)
{
    std::printf("  test_find_enemy\n");

    const EnemyData* skeleton = db.findEnemy("skeleton");
    ASSERT(skeleton != nullptr, "findEnemy('skeleton') not null");
    if (skeleton) {
        ASSERT(skeleton->maxHp > 0,  "skeleton maxHp > 0");
        ASSERT(!skeleton->id.empty(), "skeleton id not empty");
        ASSERT(skeleton->expReward > 0, "skeleton expReward > 0");
    }
}

// ── Test: findLootTableForEnemy returns drops for skeleton ────────────────────
static void test_find_loot_table(GameplayDatabase& db)
{
    std::printf("  test_find_loot_table\n");

    const LootTableData* table = db.findLootTableForEnemy("skeleton");
    ASSERT(table != nullptr, "findLootTableForEnemy('skeleton') not null");
    if (table) {
        ASSERT(!table->drops.empty(), "skeleton loot table has drops");
        // gold should be one of the drops
        bool hasGold = false;
        for (auto& d : table->drops) {
            if (d.item == "gold") { hasGold = true; break; }
        }
        ASSERT(hasGold, "skeleton loot table contains gold drop");
    }
}

// ── Test: lookup of unknown entity returns nullptr ────────────────────────────
static void test_missing_lookup(GameplayDatabase& db)
{
    std::printf("  test_missing_lookup\n");

    const PlayerClassData* pc  = db.findPlayerClass("no_such_class");
    const EnemyData*       en  = db.findEnemy("no_such_enemy");
    const LootTableData*   lt  = db.findLootTableForEnemy("no_such_enemy");

    ASSERT(pc  == nullptr, "findPlayerClass unknown → nullptr");
    ASSERT(en  == nullptr, "findEnemy unknown → nullptr");
    ASSERT(lt  == nullptr, "findLootTableForEnemy unknown → nullptr");
}

// ── Test: load() with nonexistent directory returns false ─────────────────────
static void test_load_invalid_dir()
{
    std::printf("  test_load_invalid_dir\n");

    GameplayDatabase db2;
    bool ok = db2.load("/nonexistent/path/that/does/not/exist");
    ASSERT(!ok, "load() returns false for invalid directory");
}

// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("=== test_gameplay_database ===\n");
    std::printf("  Assets dir: %s\n", ASSETS_DIR);

    GameplayDatabase db;
    test_load_valid_dir(db);
    test_find_player_class(db);
    test_find_enemy(db);
    test_find_loot_table(db);
    test_missing_lookup(db);
    test_load_invalid_dir();

    std::printf("Result: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
