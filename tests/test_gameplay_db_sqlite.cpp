#include "game/data/GameplayDatabase.h"
#include "project/ProjectDataMigrator.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { \
        ++g_pass; \
    } \
} while(0)

static bool writeTextFile(const fs::path& path, const std::string& content)
{
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << content;
    return out.good();
}

int main()
{
    std::printf("=== test_gameplay_db_sqlite ===\n");

    const fs::path root = fs::temp_directory_path() / "dash_test_gameplay_db_sqlite";
    const fs::path gameplayDir = root / "assets" / "gameplay";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(gameplayDir, ec);
    fs::create_directories(root / ".library", ec);
    fs::create_directories(root / "scenes", ec);

    ASSERT(writeTextFile(root / "assets" / "asset_db.json", "{\"assets\":[]}"), "write asset_db json");

    ASSERT(writeTextFile(gameplayDir / "player_classes.json", R"JSON([
      {
        "id":"warrior",
        "name":"Warrior",
        "description":"frontline",
        "maxHp":120,
        "maxMana":30,
        "attackCooldown":1.0,
        "stats":{"attack":15,"defense":10,"magicAttack":0,"speed":2.5,"critChance":0.07}
      }
    ])JSON"), "write player_classes json");

    ASSERT(writeTextFile(gameplayDir / "enemies.json", R"JSON([
      {
        "id":"goblin",
        "name":"Goblin",
        "maxHp":50,
        "detectionRadius":6.0,
        "attackRadius":1.2,
        "expReward":20,
        "attackCooldown":1.0,
        "stats":{"attack":8,"defense":4,"magicAttack":0,"speed":2.7,"critChance":0.04}
      }
    ])JSON"), "write enemies json");

    ASSERT(writeTextFile(gameplayDir / "loot_tables.json", R"JSON([
      {
        "id":"goblin_loot",
        "enemies":["goblin"],
        "drops":[{"item":"gold","chance":0.7,"minQty":1,"maxQty":3}]
      }
    ])JSON"), "write loot json");

    ASSERT(writeTextFile(root / "scenes" / "default.json", R"JSON({
      "sceneVersion":2,
      "name":"Default",
      "worldSeed":12345,
      "nextEntityId":1,
      "tileOverrides":[],
      "entities":[]
    })JSON"), "write default scene json");

    ProjectManifest manifest;
    manifest.name = "GameplaySqliteTest";
    manifest.projectRoot = root.string();

    auto migration = ProjectDataMigrator::migrateJsonToSqlite(manifest);
    ASSERT(migration.success, "project migration succeeds");

    GameplayDatabase db;
    ASSERT(db.load((root / "assets").string()), "gameplay db load");

    const PlayerClassData* warrior = db.findPlayerClass("warrior");
    const EnemyData* goblin = db.findEnemy("goblin");
    const LootTableData* loot = db.findLootTableForEnemy("goblin");

    ASSERT(warrior != nullptr, "player class loaded from sqlite");
    ASSERT(goblin != nullptr, "enemy loaded from sqlite");
    ASSERT(loot != nullptr, "loot table loaded from sqlite");
    if (loot) {
        ASSERT(!loot->drops.empty(), "loot drops present");
    }

    fs::remove_all(root, ec);
    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
