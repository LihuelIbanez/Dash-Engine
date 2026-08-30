#include "project/ProjectDataMigrator.h"
#include "project/ProjectManifest.h"
#include "db/SqliteDb.h"

#include <sqlite3.h>

#include <cstdio>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

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

static ProjectManifest createTempProject(const fs::path& root, const std::string& name)
{
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "assets" / "gameplay", ec);
    fs::create_directories(root / "scenes", ec);
    fs::create_directories(root / ".library", ec);

    ProjectManifest m;
    m.name = name;
    m.projectRoot = root.string();
    m.saveToFile((root / (name + ".dashproject")).string());
    return m;
}

static bool seedGameplayAndAssets(const fs::path& root,
                                  const std::string& playerClassesJson,
                                  const std::string& enemiesJson,
                                  const std::string& lootJson)
{
    const bool okAssets = writeTextFile(root / "assets" / "asset_db.json", R"JSON({
  "assets": [
    {
      "guid": "asset-guid-1",
      "sourcePath": "assets/sprites/player.png",
      "importPath": ".library/sprites/player.bin",
      "assetType": "Texture",
      "hash": "deadbeef",
      "lastImportTime": 123456,
      "dependencies": ["assets/sprites/default_palette.json"]
    }
  ]
})JSON");

    const bool okClasses = writeTextFile(root / "assets" / "gameplay" / "player_classes.json", playerClassesJson);
    const bool okEnemies = writeTextFile(root / "assets" / "gameplay" / "enemies.json", enemiesJson);
    const bool okLoot = writeTextFile(root / "assets" / "gameplay" / "loot_tables.json", lootJson);

    return okAssets && okClasses && okEnemies && okLoot;
}

static int queryInt(const std::string& dbPath, const std::string& sql)
{
    SqliteDb db;
    std::string error;
    if (!db.open(dbPath, &error)) {
        return -1;
    }

    auto stmt = db.prepare(sql, &error);
    if (!stmt.isValid()) {
        return -1;
    }

    if (stmt.step() != SQLITE_ROW) {
        return -1;
    }
    return stmt.columnInt(0);
}

// ── La cache de escenas sigue al disco ───────────────────────────────────────
static void test_scene_sync_follows_disk()
{
    std::printf("  test_scene_sync_follows_disk\n");

    const fs::path root = fs::temp_directory_path() / "dash_test_scene_sync";
    const ProjectManifest manifest = createTempProject(root, "scene_sync");

    const std::string sceneV1 = R"JSON({
  "sceneVersion": 6, "name": "arena", "worldSeed": 7, "nextEntityId": 2,
  "tileOverrides": [],
  "entities": [ { "id": 1, "type": "Player", "name": "Hero", "x": 1.0, "y": 2.0 } ]
})JSON";
    ASSERT(writeTextFile(root / "scenes" / "arena.json", sceneV1), "escribe la escena inicial");

    // Primera pasada: la fila no existe, así que se importa.
    auto first = ProjectDataMigrator::syncScenesFromDisk(manifest);
    ASSERT(first.success, "la primera sincronizacion tiene exito");
    ASSERT(first.summary.imported == 1, "importa la escena que falta");
    ASSERT(first.summary.upToDate == 0, "nada estaba al dia todavia");

    // Segunda pasada sin tocar el archivo: no debe reimportar.
    auto second = ProjectDataMigrator::syncScenesFromDisk(manifest);
    ASSERT(second.success, "la segunda sincronizacion tiene exito");
    ASSERT(second.summary.imported == 0, "un .json sin cambios no se reimporta");
    ASSERT(second.summary.upToDate == 1, "la fila queda marcada al dia");

    // El disco manda: un .json mas nuevo tiene que pisar la cache. La mtime se
    // adelanta a mano porque el reloj del filesystem tiene poca resolucion y dos
    // escrituras seguidas pueden compartir timestamp.
    const std::string sceneV2 = R"JSON({
  "sceneVersion": 6, "name": "arena", "worldSeed": 99, "nextEntityId": 3,
  "tileOverrides": [],
  "entities": [
    { "id": 1, "type": "Player", "name": "Hero",  "x": 1.0, "y": 2.0 },
    { "id": 2, "type": "Enemy",  "name": "Wolf",  "x": 5.0, "y": 6.0 }
  ]
})JSON";
    const fs::path scenePath = root / "scenes" / "arena.json";
    ASSERT(writeTextFile(scenePath, sceneV2), "reescribe la escena");

    std::error_code ec;
    fs::last_write_time(scenePath,
                        fs::file_time_type::clock::now() + std::chrono::seconds(5), ec);
    ASSERT(!ec, "puede adelantar la mtime");

    auto third = ProjectDataMigrator::syncScenesFromDisk(manifest);
    ASSERT(third.success, "la tercera sincronizacion tiene exito");
    ASSERT(third.summary.imported == 1, "un .json mas nuevo se reimporta");

    // Y lo reimportado es de verdad la version nueva, no la vieja.
    ASSERT(queryInt(third.dbPath, "SELECT world_seed FROM scenes WHERE file_name='arena.json';") == 99,
           "la fila refleja el contenido nuevo");

    fs::remove_all(root, ec);
}

int main()
{
    std::printf("=== test_project_data_migrator ===\n");

    test_scene_sync_follows_disk();

    const fs::path base = fs::temp_directory_path() / "dash_test_project_data_migrator";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);

    const std::string kClassesOne = R"JSON([
  {
    "id": "warrior",
    "name": "Warrior",
    "description": "Frontline",
    "maxHp": 140,
    "maxMana": 30,
    "attackCooldown": 1.0,
    "stats": {
      "attack": 16,
      "defense": 10,
      "magicAttack": 2,
      "speed": 2.6,
      "critChance": 0.08
    }
  }
])JSON";

    const std::string kClassesTwo = R"JSON([
  {
    "id": "warrior",
    "name": "Warrior",
    "description": "Frontline",
    "maxHp": 140,
    "maxMana": 30,
    "attackCooldown": 1.0,
    "stats": {
      "attack": 16,
      "defense": 10,
      "magicAttack": 2,
      "speed": 2.6,
      "critChance": 0.08
    }
  },
  {
    "id": "mage",
    "name": "Mage",
    "description": "Ranged caster",
    "maxHp": 90,
    "maxMana": 120,
    "attackCooldown": 1.1,
    "stats": {
      "attack": 6,
      "defense": 4,
      "magicAttack": 20,
      "speed": 2.2,
      "critChance": 0.12
    }
  }
])JSON";

    const std::string kEnemies = R"JSON([
  {
    "id": "goblin",
    "name": "Goblin",
    "maxHp": 45,
    "detectionRadius": 6.0,
    "attackRadius": 1.2,
    "expReward": 25,
    "attackCooldown": 1.0,
    "stats": {
      "attack": 8,
      "defense": 3,
      "magicAttack": 0,
      "speed": 2.7,
      "critChance": 0.04
    }
  }
])JSON";

    const std::string kLoot = R"JSON([
  {
    "id": "goblin_common",
    "enemies": ["goblin"],
    "drops": [
      {"item": "gold", "chance": 0.8, "minQty": 1, "maxQty": 5}
    ]
  }
])JSON";

    // Case 1: success path
    {
        const fs::path root = base / "success_project";
        ProjectManifest manifest = createTempProject(root, "SuccessProject");
        ASSERT(seedGameplayAndAssets(root, kClassesOne, kEnemies, kLoot), "seed success project files");

        auto result = ProjectDataMigrator::migrateJsonToSqlite(manifest);
        ASSERT(result.success, "migration succeeds with valid JSON data");
        ASSERT(fs::exists(root / ".library" / "dash_engine.db"), "sqlite db is created");
        ASSERT(queryInt(result.dbPath, "SELECT COUNT(*) FROM player_classes;") == 1, "player_classes migrated");
        ASSERT(queryInt(result.dbPath, "SELECT COUNT(*) FROM enemies;") == 1, "enemies migrated");
        ASSERT(queryInt(result.dbPath, "SELECT COUNT(*) FROM loot_tables;") == 1, "loot tables migrated");
        ASSERT(queryInt(result.dbPath, "SELECT COUNT(*) FROM assets;") == 1, "assets migrated");
    }

    // Case 2: fallback trigger (invalid/missing gameplay data)
    {
        const fs::path root = base / "fallback_project";
        ProjectManifest manifest = createTempProject(root, "FallbackProject");
        ASSERT(writeTextFile(root / "assets" / "asset_db.json", "{\"assets\":[]}"), "seed minimal asset db");

        auto result = ProjectDataMigrator::migrateJsonToSqlite(manifest);
        ASSERT(!result.success, "migration fails when gameplay json files are missing");
        ASSERT(!result.log.empty(), "migration failure emits log lines");
    }

    // Case 3: rollback on mid-migration failure keeps previous DB content
    {
        const fs::path root = base / "rollback_project";
        ProjectManifest manifest = createTempProject(root, "RollbackProject");
        ASSERT(seedGameplayAndAssets(root, kClassesOne, kEnemies, kLoot), "seed rollback project files");

        auto first = ProjectDataMigrator::migrateJsonToSqlite(manifest);
        ASSERT(first.success, "initial migration succeeds");
        ASSERT(queryInt(first.dbPath, "SELECT COUNT(*) FROM player_classes;") == 1, "baseline player_classes count is 1");

        ASSERT(writeTextFile(root / "assets" / "gameplay" / "player_classes.json", kClassesTwo), "overwrite classes with 2 entries");
        ASSERT(writeTextFile(root / "assets" / "gameplay" / "enemies.json", "{invalid json"), "corrupt enemies json to force failure");

        auto second = ProjectDataMigrator::migrateJsonToSqlite(manifest);
        ASSERT(!second.success, "migration fails on corrupt enemies json");

        const int classesAfterFail = queryInt(first.dbPath, "SELECT COUNT(*) FROM player_classes;");
        const int mageRows = queryInt(first.dbPath, "SELECT COUNT(*) FROM player_classes WHERE id='mage';");
        ASSERT(classesAfterFail == 1, "rollback preserves previous player_classes row count");
        ASSERT(mageRows == 0, "rollback prevents partial insert from failed run");
    }

    fs::remove_all(base, ec);

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
