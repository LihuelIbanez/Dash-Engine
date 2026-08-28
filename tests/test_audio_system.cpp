// ─────────────────────────────────────────────────────────────────────────────
// AudioSystem tests — GUID/path clip resolution, playOnStart firing once,
// emitter position tracking and listener placement.
// Everything runs headless: AudioEngine::init() is never called, so every
// engine call is a no-op and the assertions target observable system state.
// ─────────────────────────────────────────────────────────────────────────────

#include "AssetDatabase.h"
#include "AudioEmitter.h"
#include "AudioEngine.h"
#include "AudioSystem.h"
#include "RuntimeContext.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

#define ASSERT_NEAR(a, b, tol, msg) do { \
    if (std::fabs((a) - (b)) > (tol)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s (got %f, want %f)\n", \
                     __FILE__, __LINE__, msg, static_cast<double>(a), static_cast<double>(b)); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

using json = nlohmann::json;

namespace {

// A scene with one enemy carrying an AudioComponent and a matching runtime enemy.
struct Fixture {
    AudioEngine  engine;                       // never initialised: headless no-op engine
    Player       player{10.f, 10.f, CharacterClass::Warrior};
    std::vector<std::unique_ptr<Enemy>> enemies;
    std::vector<AudioEmitter>           emitters;
    RuntimeContext ctx;

    Fixture()
    {
        enemies.push_back(std::make_unique<Enemy>(12.f, 10.f, std::string("skeleton")));

        AudioEmitter e;
        e.name            = "Skeleton";
        e.component.clip  = "audio/growl.wav";
        e.component.loop  = true;
        e.attachment      = AudioEmitter::Attachment::Enemy;
        e.enemyIndex      = 0;
        emitters.push_back(e);

        ctx.player        = &player;
        ctx.enemies       = &enemies;
        ctx.audioEmitters = &emitters;
    }
};

AudioSystem makeSystem(AudioEngine& engine, const AssetDatabase* db = nullptr)
{
    return AudioSystem(&engine, db, std::string{});
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: looksLikeGuid
// ─────────────────────────────────────────────────────────────────────────────
static void test_looks_like_guid()
{
    std::printf("  test_looks_like_guid\n");
    using dash::audio::looksLikeGuid;

    ASSERT(looksLikeGuid("f81d4fae-7dec-11d0-a765-00a0c91e6bf6"), "well-formed GUID accepted");
    ASSERT(looksLikeGuid("F81D4FAE-7DEC-11D0-A765-00A0C91E6BF6"), "uppercase hex accepted");

    ASSERT(!looksLikeGuid(""),                     "empty string rejected");
    ASSERT(!looksLikeGuid("audio/growl.wav"),      "relative path rejected");
    ASSERT(!looksLikeGuid("/tmp/audio/growl.wav"), "absolute path rejected");
    ASSERT(!looksLikeGuid("f81d4fae"),             "short string rejected");
    ASSERT(!looksLikeGuid("f81d4fae-7dec-11d0-a765-00a0c91e6bf6-extra"), "long string rejected");

    // Right length, wrong content.
    ASSERT(!looksLikeGuid("g81d4fae-7dec-11d0-a765-00a0c91e6bf6"), "non-hex digit rejected");
    ASSERT(!looksLikeGuid("f81d4fae7-dec-11d0-a765-00a0c91e6bf6"), "misplaced dashes rejected");
    ASSERT(!looksLikeGuid("f81d4fae-7dec-11d0-a765-00a0c91e6bf-6"), "trailing dash position rejected");
    ASSERT(!looksLikeGuid("------------------------------------"), "all dashes rejected");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: clip resolution against the asset database
// ─────────────────────────────────────────────────────────────────────────────
static void test_clip_resolution()
{
    std::printf("  test_clip_resolution\n");
    using dash::audio::resolveClipPath;

    const std::string knownGuid   = "f81d4fae-7dec-11d0-a765-00a0c91e6bf6";
    const std::string unknownGuid = "00000000-0000-0000-0000-000000000000";

    AssetDatabase db;
    AssetRecord rec;
    rec.guid       = knownGuid;
    rec.sourcePath = "audio/sfx/sword_hit.wav";
    rec.assetType  = AssetType::Audio;
    db.upsertRecord(rec);

    ASSERT(resolveClipPath(knownGuid, &db) == "audio/sfx/sword_hit.wav",
           "known GUID resolves to its sourcePath");
    ASSERT(resolveClipPath(unknownGuid, &db) == unknownGuid,
           "unknown GUID falls back to the original string");
    ASSERT(resolveClipPath("audio/growl.wav", &db) == "audio/growl.wav",
           "path reference is passed through untouched");

    // Without a database only path references can work, and they still do.
    ASSERT(resolveClipPath(knownGuid, nullptr) == knownGuid,
           "GUID without a database falls back to the original string");
    ASSERT(resolveClipPath("audio/growl.wav", nullptr) == "audio/growl.wav",
           "path reference works without a database");
    ASSERT(resolveClipPath("", &db).empty(), "empty clip stays empty");

    // A record with no sourcePath cannot resolve to anything useful.
    AssetRecord empty;
    empty.guid = unknownGuid;
    db.upsertRecord(empty);
    ASSERT(resolveClipPath(unknownGuid, &db) == unknownGuid,
           "record without sourcePath falls back to the original string");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: playOnStart fires exactly once
// ─────────────────────────────────────────────────────────────────────────────
static void test_play_on_start_fires_once()
{
    std::printf("  test_play_on_start_fires_once\n");

    Fixture f;
    AudioSystem system = makeSystem(f.engine);

    ASSERT(f.emitters[0].playCount == 0, "emitter is idle before the first update");

    for (int i = 0; i < 8; ++i) system.update(f.ctx);

    ASSERT(f.emitters[0].started, "emitter is marked as started");
    ASSERT(f.emitters[0].playCount == 1, "playOnStart fires once, not once per frame");

    // playOnStart == false never triggers playback at all.
    Fixture quiet;
    quiet.emitters[0].component.playOnStart = false;
    AudioSystem quietSystem = makeSystem(quiet.engine);
    for (int i = 0; i < 4; ++i) quietSystem.update(quiet.ctx);
    ASSERT(quiet.emitters[0].playCount == 0, "playOnStart == false never fires");

    // An emitter without a clip has nothing to play.
    Fixture noClip;
    noClip.emitters[0].component.clip.clear();
    AudioSystem noClipSystem = makeSystem(noClip.engine);
    noClipSystem.update(noClip.ctx);
    ASSERT(noClip.emitters[0].playCount == 0, "emitter without a clip never fires");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: emitters follow their entity, and stop when it dies or is disabled
// ─────────────────────────────────────────────────────────────────────────────
static void test_emitter_follows_entity()
{
    std::printf("  test_emitter_follows_entity\n");

    Fixture f;
    AudioSystem system = makeSystem(f.engine);
    system.update(f.ctx);

    ASSERT_NEAR(f.emitters[0].x, 12.f, 1e-5f, "emitter starts at the enemy position");

    f.enemies[0]->x = 25.f;
    f.enemies[0]->y = 4.f;
    f.enemies[0]->z = 2.f;
    system.update(f.ctx);
    ASSERT_NEAR(f.emitters[0].x, 25.f, 1e-5f, "emitter follows the enemy on x");
    ASSERT_NEAR(f.emitters[0].y,  4.f, 1e-5f, "emitter follows the enemy on y");
    ASSERT_NEAR(f.emitters[0].z,  2.f, 1e-5f, "emitter follows the enemy on z");

    // A dead entity leaves no dangling voice and does not re-trigger.
    f.enemies[0]->alive = false;
    system.update(f.ctx);
    ASSERT(f.emitters[0].voice == AudioEngine::kInvalidVoice, "dead entity stops its voice");
    ASSERT(f.emitters[0].playCount == 1, "dead entity does not re-trigger playback");

    // An emitter whose entity vanished from the list is silenced too.
    Fixture gone;
    AudioSystem goneSystem = makeSystem(gone.engine);
    goneSystem.update(gone.ctx);
    gone.enemies.clear();
    goneSystem.update(gone.ctx);
    ASSERT(gone.emitters[0].voice == AudioEngine::kInvalidVoice, "missing entity stops its voice");

    // A disabled component is silent and keeps its trigger count.
    Fixture disabled;
    AudioSystem disabledSystem = makeSystem(disabled.engine);
    disabledSystem.update(disabled.ctx);
    disabled.emitters[0].enabled = false;
    const float lastX = disabled.emitters[0].x;
    disabled.enemies[0]->x = 99.f;
    disabledSystem.update(disabled.ctx);
    ASSERT(disabled.emitters[0].voice == AudioEngine::kInvalidVoice, "disabled component stops its voice");
    ASSERT_NEAR(disabled.emitters[0].x, lastX, 1e-5f, "disabled component stops tracking");

    // A static emitter keeps the position the scene gave it.
    Fixture fixed;
    fixed.emitters[0].attachment = AudioEmitter::Attachment::Static;
    fixed.emitters[0].enemyIndex = -1;
    fixed.emitters[0].x = 3.f;
    fixed.emitters[0].y = 7.f;
    AudioSystem fixedSystem = makeSystem(fixed.engine);
    fixed.enemies[0]->x = 40.f;
    fixedSystem.update(fixed.ctx);
    ASSERT_NEAR(fixed.emitters[0].x, 3.f, 1e-5f, "static emitter keeps its scene position");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: the listener rides the isometric camera (the player)
// ─────────────────────────────────────────────────────────────────────────────
static void test_listener_follows_camera()
{
    std::printf("  test_listener_follows_camera\n");

    Fixture f;
    AudioSystem system = makeSystem(f.engine);

    f.player.x = 5.f;
    f.player.y = 7.f;
    f.player.z = 1.f;
    system.update(f.ctx);

    const dash::audio::ListenerState& l = f.engine.listener();
    ASSERT_NEAR(l.position.x,  5.f, 1e-5f, "listener x tracks the camera");
    ASSERT_NEAR(l.position.y,  1.f, 1e-5f, "listener y is the world height");
    ASSERT_NEAR(l.position.z, -7.f, 1e-5f, "listener z mirrors the world y axis");

    f.player.x = -3.f;
    system.update(f.ctx);
    ASSERT_NEAR(f.engine.listener().position.x, -3.f, 1e-5f, "listener keeps tracking the camera");

    // Screen-right in the isometric projection is world (+x,-y): it must pan right.
    AudioEngine::PlayParams right;
    right.spatial = true;
    const dash::audio::Vec3 rightPos = dash::audio::worldToAudio(f.player.x + 4.f, f.player.y - 4.f, 0.f);
    right.x = rightPos.x; right.y = rightPos.y; right.z = rightPos.z;
    right.maxDistance = 100.f;
    ASSERT(f.engine.gainFor(right).pan > 0.f, "emitter on the screen right pans right");

    AudioEngine::PlayParams left = right;
    const dash::audio::Vec3 leftPos = dash::audio::worldToAudio(f.player.x - 4.f, f.player.y + 4.f, 0.f);
    left.x = leftPos.x; left.y = leftPos.y; left.z = leftPos.z;
    ASSERT(f.engine.gainFor(left).pan < 0.f, "emitter on the screen left pans left");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6: entities without an AudioComponent produce no emitter
// ─────────────────────────────────────────────────────────────────────────────
static void test_entity_without_audio_is_ignored()
{
    std::printf("  test_entity_without_audio_is_ignored\n");

    ASSERT(!audioEmitterFromEntityJson(json::parse(R"({"id":1,"name":"Hero","x":1,"y":2})")).has_value(),
           "entity without components is ignored");

    const json onlyRender = json::parse(R"({
        "id": 2, "name": "Skeleton", "x": 4, "y": 5,
        "components": [{"type": "Render", "sprite": "skeleton"}]
    })");
    ASSERT(!audioEmitterFromEntityJson(onlyRender).has_value(),
           "entity with other components but no Audio is ignored");

    ASSERT(!audioEmitterFromEntityJson(json::parse("[1,2,3]")).has_value(),
           "non-object entity is ignored");

    const json withAudio = json::parse(R"({
        "id": 7, "name": "Torch", "x": 4.5, "y": 6.5,
        "components": [
            {"type": "Transform", "x": 4.5, "y": 6.5, "z": 1.5},
            {"type": "Audio", "clip": "audio/fire.wav", "volume": 0.4, "pitch": 1.2,
             "loop": true, "playOnStart": false, "spatial": true,
             "minDistance": 2.0, "maxDistance": 15.0, "bus": 2}
        ]
    })");
    const std::optional<AudioEmitter> emitter = audioEmitterFromEntityJson(withAudio);
    ASSERT(emitter.has_value(), "entity with an AudioComponent produces an emitter");
    if (emitter) {
        ASSERT(emitter->entityId == 7,               "entity id is carried over");
        ASSERT(emitter->name == "Torch",             "entity name is carried over");
        ASSERT(emitter->component.clip == "audio/fire.wav", "clip is read");
        ASSERT(!emitter->component.playOnStart,      "playOnStart is read");
        ASSERT(emitter->component.loop,              "loop is read");
        ASSERT(emitter->component.bus == 2,          "bus is read");
        ASSERT_NEAR(emitter->component.volume,      0.4f,  1e-5f, "volume is read");
        ASSERT_NEAR(emitter->component.minDistance, 2.0f,  1e-5f, "minDistance is read");
        ASSERT_NEAR(emitter->component.maxDistance, 15.0f, 1e-5f, "maxDistance is read");
        ASSERT_NEAR(emitter->x, 4.5f, 1e-5f, "position comes from the entity");
        ASSERT_NEAR(emitter->z, 1.5f, 1e-5f, "height comes from the Transform component");
    }

    // An Audio component without fields falls back to the component defaults.
    const json defaults = json::parse(R"({
        "id": 8, "x": 0, "y": 0, "components": [{"type": "Audio"}]
    })");
    const std::optional<AudioEmitter> bare = audioEmitterFromEntityJson(defaults);
    ASSERT(bare.has_value(), "bare Audio component still produces an emitter");
    if (bare) {
        ASSERT(bare->component.clip.empty(), "missing clip defaults to empty");
        ASSERT(bare->component.playOnStart,  "playOnStart defaults to true");
        ASSERT(bare->component.bus == static_cast<int>(AudioBus::Sfx), "bus defaults to Sfx");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7: the system survives a context without a scene
// ─────────────────────────────────────────────────────────────────────────────
static void test_empty_context()
{
    std::printf("  test_empty_context\n");

    AudioEngine engine;
    AudioSystem system = makeSystem(engine);

    RuntimeContext ctx;
    system.update(ctx);          // no player, no emitters
    ASSERT(!engine.isInitialized(), "headless engine stays uninitialised");

    std::vector<AudioEmitter> none;
    ctx.audioEmitters = &none;
    system.update(ctx);
    ASSERT(none.empty(), "an empty scene keeps an empty emitter list");
}

int main()
{
    std::printf("Running AudioSystem tests...\n");

    test_looks_like_guid();
    test_clip_resolution();
    test_play_on_start_fires_once();
    test_emitter_follows_entity();
    test_listener_follows_camera();
    test_entity_without_audio_is_ignored();
    test_empty_context();

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
