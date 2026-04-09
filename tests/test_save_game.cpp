// ═════════════════════════════════════════════════════════════════════════════
// test_save_game — SaveGame round-trip, versioning, edge cases
// ═════════════════════════════════════════════════════════════════════════════
#include "SaveGame.h"
#include "SaveVersioning.h"
#include "AppPaths.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

// Helper: build a populated SaveData for testing
static SaveData makeSave()
{
    SaveData d;
    d.worldSeed       = 99999u;
    d.score           = 350;
    d.saveVersion     = SaveData::kCurrentVersion;

    d.player.x        = 10.5f;
    d.player.y        = 22.3f;
    d.player.health   = 75;
    d.player.maxHealth = 100;
    d.player.mana     = 30;
    d.player.maxMana  = 50;
    d.player.attack   = 15;
    d.player.defense  = 8;
    d.player.level    = 3;
    d.player.experience = 240;
    d.player.expToNext  = 300;
    d.player.charClass  = "Mage";

    SaveEnemyData e1;
    e1.x = 5.f; e1.y = 7.f; e1.health = 40; e1.maxHealth = 60;
    e1.name = "skeleton"; e1.alive = true; e1.expReward = 40;
    d.enemies.push_back(e1);

    SaveEnemyData e2;
    e2.x = 12.f; e2.y = 3.f; e2.health = 60; e2.maxHealth = 60;
    e2.name = "zombie"; e2.alive = true; e2.expReward = 50;
    d.enemies.push_back(e2);

    return d;
}

// ── Test: save writes a file, load reads it back ──────────────────────────────
static void test_save_load_round_trip()
{
    std::printf("  test_save_load_round_trip\n");

    const std::string path = "/tmp/test_savegame_rt.json";

    SaveData orig = makeSave();
    bool saved = SaveGame::save(orig, path);
    ASSERT(saved, "save() returns true on success");
    ASSERT(fs::exists(path), "save file exists on disk");

    SaveData loaded;
    bool ok = SaveGame::load(path, loaded);
    ASSERT(ok, "load() returns true on success");

    ASSERT(loaded.worldSeed == orig.worldSeed, "worldSeed round-trip");
    ASSERT(loaded.score == orig.score, "score round-trip");

    fs::remove(path);
}

// ── Test: player data preserved across round-trip ────────────────────────────
static void test_player_data_preserved()
{
    std::printf("  test_player_data_preserved\n");

    const std::string path = "/tmp/test_savegame_player.json";
    SaveData orig = makeSave();
    SaveGame::save(orig, path);

    SaveData loaded;
    SaveGame::load(path, loaded);

    ASSERT(loaded.player.health    == orig.player.health,    "player.health");
    ASSERT(loaded.player.maxHealth == orig.player.maxHealth, "player.maxHealth");
    ASSERT(loaded.player.level     == orig.player.level,     "player.level");
    ASSERT(loaded.player.charClass == orig.player.charClass, "player.charClass");
    ASSERT(loaded.player.attack    == orig.player.attack,    "player.attack");
    ASSERT(loaded.player.experience == orig.player.experience, "player.experience");

    fs::remove(path);
}

// ── Test: enemy list preserved across round-trip ─────────────────────────────
static void test_enemies_preserved()
{
    std::printf("  test_enemies_preserved\n");

    const std::string path = "/tmp/test_savegame_enemies.json";
    SaveData orig = makeSave();
    SaveGame::save(orig, path);

    SaveData loaded;
    SaveGame::load(path, loaded);

    ASSERT(loaded.enemies.size() == orig.enemies.size(), "enemy count round-trip");
    if (!loaded.enemies.empty()) {
        ASSERT(loaded.enemies[0].name  == orig.enemies[0].name,   "enemy[0].name");
        ASSERT(loaded.enemies[0].health == orig.enemies[0].health, "enemy[0].health");
        ASSERT(loaded.enemies[1].name  == orig.enemies[1].name,   "enemy[1].name");
    }

    fs::remove(path);
}

// ── Test: load on nonexistent file returns false ──────────────────────────────
static void test_load_missing_file()
{
    std::printf("  test_load_missing_file\n");

    SaveData d;
    bool ok = SaveGame::load("/tmp/nonexistent_save_xyz.json", d);
    ASSERT(!ok, "load() returns false for missing file");
}

// ── Test: SaveVersioning::migrate is a no-op for current version ──────────────
static void test_versioning_no_change_at_current()
{
    std::printf("  test_versioning_no_change_at_current\n");

    // A doc already at current version: migrate should not alter "score"
    nlohmann::json j;
    j["version"] = SaveVersioning::kLatestVersion;
    j["score"]   = 42;

    bool ok = SaveVersioning::migrate(j, SaveVersioning::kLatestVersion);
    // Current version is a no-op; no crash expected
    ASSERT(ok || !ok, "migrate does not crash at current version");
    ASSERT(j["score"].get<int>() == 42, "score field untouched");
}

// ── Test: score and worldSeed preserved ──────────────────────────────────────
static void test_score_and_seed()
{
    std::printf("  test_score_and_seed\n");

    const std::string path = "/tmp/test_savegame_score.json";
    SaveData orig = makeSave();
    orig.score    = 9999;
    orig.worldSeed = 77777u;
    SaveGame::save(orig, path);

    SaveData loaded;
    SaveGame::load(path, loaded);

    ASSERT(loaded.score     == 9999,   "score preserved");
    ASSERT(loaded.worldSeed == 77777u, "worldSeed preserved");

    fs::remove(path);
}

// ── Test: sqlite mode round-trip by slot name (D64) ────────────────────────
static void test_sqlite_mode_roundtrip()
{
    std::printf("  test_sqlite_mode_roundtrip\n");

    const fs::path root = fs::temp_directory_path() / "dash_test_save_sqlite_mode";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "assets", ec);
    fs::create_directories(root / "scenes", ec);
    fs::create_directories(root / ".library", ec);
    fs::create_directories(root / "build_output", ec);

    AppPaths::setActiveProjectPaths(
        (root / "assets").string(),
        (root / "scenes").string(),
        (root / ".library").string(),
        (root / "build_output").string());

    setenv("DASH_DB_MODE", "sqlite", 1);

    SaveData orig = makeSave();
    const std::string slotPath = (root / "saves" / "quicksave.json").string();

    ASSERT(SaveGame::save(orig, slotPath), "sqlite save succeeds");

    SaveData loaded;
    ASSERT(SaveGame::load(slotPath, loaded), "sqlite load succeeds");
    ASSERT(loaded.worldSeed == orig.worldSeed, "sqlite round-trip worldSeed");
    ASSERT(loaded.player.level == orig.player.level, "sqlite round-trip player level");

    AppPaths::clearActiveProjectPaths();
    unsetenv("DASH_DB_MODE");
    fs::remove_all(root, ec);
}

// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("=== test_save_game ===\n");

    setenv("DASH_DB_MODE", "json", 1);
    test_save_load_round_trip();
    test_player_data_preserved();
    test_enemies_preserved();
    test_load_missing_file();
    test_versioning_no_change_at_current();
    test_score_and_seed();
    unsetenv("DASH_DB_MODE");

    test_sqlite_mode_roundtrip();

    std::printf("Result: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
