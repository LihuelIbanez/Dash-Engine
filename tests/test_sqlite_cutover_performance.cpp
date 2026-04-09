#include "project/ProjectDataMigrator.h"
#include "scene/SceneRepositorySqlite.h"
#include "game/data/GameplayDatabase.h"
#include "SceneData.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

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

static void seedProject(const fs::path& root, int sceneCount)
{
    std::error_code ec;
    fs::create_directories(root / "assets" / "gameplay", ec);
    fs::create_directories(root / "scenes", ec);
    fs::create_directories(root / ".library", ec);

    writeTextFile(root / "assets" / "asset_db.json", R"JSON({"assets":[{"guid":"a1","sourcePath":"assets/sprites/s.png","importPath":".library/s.bin","assetType":"Texture","hash":"x","lastImportTime":1,"dependencies":[]}]} )JSON");

    writeTextFile(root / "assets" / "gameplay" / "player_classes.json", R"JSON([
      {"id":"warrior","name":"Warrior","description":"front","maxHp":120,"maxMana":30,"attackCooldown":1.0,
       "stats":{"attack":10,"defense":8,"magicAttack":0,"speed":3.0,"critChance":0.05}}
    ])JSON");

    writeTextFile(root / "assets" / "gameplay" / "enemies.json", R"JSON([
      {"id":"goblin","name":"Goblin","maxHp":60,"detectionRadius":6.0,"attackRadius":1.2,"expReward":20,"attackCooldown":1.0,
       "stats":{"attack":8,"defense":4,"magicAttack":0,"speed":2.8,"critChance":0.04}}
    ])JSON");

    writeTextFile(root / "assets" / "gameplay" / "loot_tables.json", R"JSON([
      {"id":"goblin_loot","enemies":["goblin"],"drops":[{"item":"gold","chance":0.8,"minQty":1,"maxQty":3}]}
    ])JSON");

    for (int i = 0; i < sceneCount; ++i) {
        const std::string name = "scene_" + std::to_string(i) + ".json";
        const std::string json =
            "{\n"
            "  \"sceneVersion\": 2,\n"
            "  \"name\": \"Scene " + std::to_string(i) + "\",\n"
            "  \"worldSeed\": 12345,\n"
            "  \"nextEntityId\": 2,\n"
            "  \"tileOverrides\": [],\n"
            "  \"entities\": [{\n"
            "    \"id\": 1,\n"
            "    \"type\": \"Player\",\n"
            "    \"name\": \"Hero\",\n"
            "    \"x\": 10.0,\n"
            "    \"y\": 10.0,\n"
            "    \"class\": \"Warrior\"\n"
            "  }]\n"
            "}\n";
        writeTextFile(root / "scenes" / name, json);
    }
}

static double msSince(const Clock::time_point& t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main()
{
    std::printf("=== test_sqlite_cutover_performance ===\n");

    const fs::path root = fs::temp_directory_path() / "dash_test_sqlite_cutover_perf";
    std::error_code ec;
    fs::remove_all(root, ec);
    seedProject(root, 200);

    ProjectManifest manifest;
    manifest.name = "PerfProject";
    manifest.projectRoot = root.string();

    auto migration = ProjectDataMigrator::migrateJsonToSqlite(manifest);
    ASSERT(migration.success, "migration for benchmark project succeeds");

    std::vector<std::string> jsonScenes;
    {
        const auto t0 = Clock::now();
        for (int it = 0; it < 20; ++it) {
            jsonScenes.clear();
            for (const auto& entry : fs::directory_iterator(root / "scenes")) {
                if (entry.path().extension() == ".json") {
                    jsonScenes.push_back(entry.path().filename().string());
                }
            }
        }
        const double jsonListMs = msSince(t0);

        SceneRepositorySqlite repo((root / ".library" / "dash_engine.db").string());
        std::vector<std::string> sqliteScenes;
        const auto t1 = Clock::now();
        for (int it = 0; it < 20; ++it) {
            sqliteScenes.clear();
            std::string err;
            ASSERT(repo.listSceneFiles(sqliteScenes, &err), "sqlite list scenes succeeds");
        }
        const double sqliteListMs = msSince(t1);

        ASSERT(!sqliteScenes.empty(), "sqlite scenes list is not empty");
        ASSERT(sqliteScenes.size() == jsonScenes.size(), "scene count parity json vs sqlite");

        SceneData s;
        const std::string firstFile = jsonScenes.front();
        const auto t2 = Clock::now();
        for (int it = 0; it < 40; ++it) {
            ASSERT(s.loadFromFile((root / "scenes" / firstFile).string(), (root / "assets").string()), "json scene load succeeds");
        }
        const double jsonLoadMs = msSince(t2);

        const auto t3 = Clock::now();
        for (int it = 0; it < 40; ++it) {
            std::string err;
            ASSERT(repo.loadScene(firstFile, s, (root / "assets").string(), &err), "sqlite scene load succeeds");
        }
        const double sqliteLoadMs = msSince(t3);

        std::printf("  JSON list 20x:    %.3f ms\n", jsonListMs);
        std::printf("  SQLite list 20x:  %.3f ms\n", sqliteListMs);
        std::printf("  JSON load 40x:    %.3f ms\n", jsonLoadMs);
        std::printf("  SQLite load 40x:  %.3f ms\n", sqliteLoadMs);

        ASSERT(sqliteListMs <= (jsonListMs * 5.0 + 1.0), "sqlite list does not regress >5x baseline");
        // Scene load includes JSON decode from DB payload and can dominate tiny baselines
        // on fast machines; keep a ratio+absolute guard to avoid flaky microbenchmark fails.
        ASSERT(sqliteLoadMs <= (jsonLoadMs * 5.0 + 6.0), "sqlite load does not regress beyond guarded threshold");

        setenv("DASH_DB_MODE", "json", 1);
        GameplayDatabase gJson;
        const auto t4 = Clock::now();
        ASSERT(gJson.load((root / "assets").string()), "json gameplay load succeeds");
        const double jsonGameplayMs = msSince(t4);

        setenv("DASH_DB_MODE", "sqlite", 1);
        GameplayDatabase gSql;
        const auto t5 = Clock::now();
        ASSERT(gSql.load((root / "assets").string()), "sqlite gameplay load succeeds");
        const double sqliteGameplayMs = msSince(t5);
        unsetenv("DASH_DB_MODE");

        std::printf("  JSON gameplay load:   %.3f ms\n", jsonGameplayMs);
        std::printf("  SQLite gameplay load: %.3f ms\n", sqliteGameplayMs);
        ASSERT(sqliteGameplayMs <= (jsonGameplayMs * 5.0 + 2.0), "sqlite gameplay load does not regress >5x baseline");
    }

    fs::remove_all(root, ec);

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
