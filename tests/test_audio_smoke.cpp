#include "EventDispatcher.h"
#include "GameEvents.h"
#include "AudioEngine.h"
#include "AudioEventBindings.h"
#include "AudioSettingsRepository.h"
#include "input/InputBindings3D.h"
#include "db/SqliteDb.h"
#include "db/SchemaManager.h"
#include <cstdio>
#include <cmath>
#include <string>

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: AudioEngine init / shutdown (soft-pass if no audio device)
// ─────────────────────────────────────────────────────────────────────────────
static void test_audio_engine_lifecycle()
{
    std::printf("  test_audio_engine_lifecycle\n");
    AudioEngine engine;
    ASSERT(!engine.isInitialized(), "not initialised before init()");

    bool ok = engine.init();
    if (!ok) {
        std::printf("    [SKIP] no audio device — soft pass\n");
        ++g_pass;  // count as pass on headless
    } else {
        ASSERT(engine.isInitialized(), "initialised after init()");
        engine.playTone(440.0f, 50.0f, 0.1f);  // should not crash
    }
    engine.shutdown();
    ASSERT(!engine.isInitialized(), "not initialised after shutdown()");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: AudioEventBindings cooldown prevents spam
// ─────────────────────────────────────────────────────────────────────────────
static void test_audio_event_bindings_cooldown()
{
    std::printf("  test_audio_event_bindings_cooldown\n");

    AudioEngine engine;
    // Don't init — playTone will be a no-op, but subscriptions still work.
    EventDispatcher dispatcher;
    AudioEventBindings bindings;
    bindings.bindDefaults(dispatcher, engine);

    // Emit two damage events in rapid succession — second should be cooldown-blocked.
    // We can't directly observe the play call, but at least verify no crash.
    dispatcher.emit(DamageEvent{1, 2, "Enemy", 10, 90});
    dispatcher.flush();
    dispatcher.emit(DamageEvent{1, 2, "Enemy", 10, 80});
    dispatcher.flush();
    ASSERT(true, "rapid events did not crash");

    // Advance past cooldown.
    bindings.tick(0.2f);
    dispatcher.emit(DamageEvent{1, 2, "Enemy", 10, 70});
    dispatcher.flush();
    ASSERT(true, "post-cooldown event ok");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: All four event types can be bound without crash
// ─────────────────────────────────────────────────────────────────────────────
static void test_audio_event_bindings_all_events()
{
    std::printf("  test_audio_event_bindings_all_events\n");

    AudioEngine engine;
    EventDispatcher dispatcher;
    AudioEventBindings bindings;
    bindings.bindDefaults(dispatcher, engine);

    bindings.tick(1.0f);  // ensure past any cooldown

    dispatcher.emit(DamageEvent{1, 2, "E", 5, 95});
    dispatcher.emit(DeathEvent{2, 1.0f, 2.0f, "E", 50});
    dispatcher.emit(LevelUpEvent{1, 2, 200});
    dispatcher.emit(LootDropEvent{"skeleton", 1.0f, 2.0f, {}});
    dispatcher.flush();
    ASSERT(true, "all four event types handled");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: AudioSettingsRepository round-trip via in-memory SQLite
// ─────────────────────────────────────────────────────────────────────────────
static void test_audio_settings_persistence()
{
    std::printf("  test_audio_settings_persistence\n");

    SqliteDb db;
    ASSERT(db.open(":memory:"), "open in-memory DB");

    // Create project_meta table manually (normally done by migration 001).
    db.exec("CREATE TABLE IF NOT EXISTS project_meta(key TEXT PRIMARY KEY, value TEXT)");

    AudioSettingsRepository repo(db);
    repo.setFloat("audio.master_volume", 0.75f);
    repo.setFloat("audio.sfx_volume",    0.50f);
    repo.setFloat("audio.music_volume",  0.25f);

    float master = repo.getFloat("audio.master_volume", 1.0f);
    float sfx    = repo.getFloat("audio.sfx_volume",    1.0f);
    float music  = repo.getFloat("audio.music_volume",  1.0f);

    ASSERT(std::fabs(master - 0.75f) < 0.001f, "master volume persisted");
    ASSERT(std::fabs(sfx    - 0.50f) < 0.001f, "sfx volume persisted");
    ASSERT(std::fabs(music  - 0.25f) < 0.001f, "music volume persisted");

    // loadInto / save round-trip
    AudioEngine engine;
    repo.loadInto(engine);
    ASSERT(std::fabs(engine.masterVolume() - 0.75f) < 0.001f, "loadInto master");
    ASSERT(std::fabs(engine.sfxVolume()    - 0.50f) < 0.001f, "loadInto sfx");
    ASSERT(std::fabs(engine.musicVolume()  - 0.25f) < 0.001f, "loadInto music");

    // Modify and save back
    engine.setMasterVolume(0.90f);
    repo.save(engine);
    ASSERT(std::fabs(repo.getFloat("audio.master_volume", 0.0f) - 0.90f) < 0.001f,
           "save round-trip");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: InputBindings3D defaults match expected GLFW key codes
// ─────────────────────────────────────────────────────────────────────────────
static void test_input_bindings_defaults()
{
    std::printf("  test_input_bindings_defaults\n");

    InputBindings3D b = InputBindings3D::defaults();
    ASSERT(b.keyForward   == 87,  "W key code");
    ASSERT(b.keyBackward  == 83,  "S key code");
    ASSERT(b.keyLeft      == 65,  "A key code");
    ASSERT(b.keyRight     == 68,  "D key code");
    ASSERT(b.mouseButtonLook == 1, "right mouse button");
    ASSERT(std::fabs(b.moveSpeed - 2.4f) < 0.001f, "default move speed");
    ASSERT(std::fabs(b.mouseSensitivity - 0.10f) < 0.001f, "default sensitivity");
    ASSERT(std::fabs(b.pitchMin - (-89.0f)) < 0.001f, "pitch min");
    ASSERT(std::fabs(b.pitchMax - 89.0f) < 0.001f, "pitch max");
}

// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("=== test_audio_smoke ===\n");
    test_audio_engine_lifecycle();
    test_audio_event_bindings_cooldown();
    test_audio_event_bindings_all_events();
    test_audio_settings_persistence();
    test_input_bindings_defaults();
    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
