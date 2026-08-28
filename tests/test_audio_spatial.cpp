// ─────────────────────────────────────────────────────────────────────────────
// Spatial audio tests — attenuation, panning and bus mixing.
// Everything here must pass on a headless CI box, so the assertions target the
// pure helpers in AudioMath.h and AudioEngine::gainFor(), none of which need a
// working audio device. The file-playback block soft-skips without one.
// ─────────────────────────────────────────────────────────────────────────────

#include "AudioEngine.h"
#include "AudioMath.h"
#include "AudioComponentBridge.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
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

using namespace dash::audio;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: distance attenuation curve
// ─────────────────────────────────────────────────────────────────────────────
static void test_distance_attenuation()
{
    std::printf("  test_distance_attenuation\n");

    const float minD = 2.0f, maxD = 10.0f;

    ASSERT_NEAR(distanceAttenuation(minD, minD, maxD), 1.0f, 1e-5f, "full volume at minDistance");
    ASSERT_NEAR(distanceAttenuation(0.0f, minD, maxD), 1.0f, 1e-5f, "full volume inside minDistance");
    ASSERT_NEAR(distanceAttenuation(maxD, minD, maxD), 0.0f, 1e-5f, "silent at maxDistance");
    ASSERT_NEAR(distanceAttenuation(6.0f, minD, maxD), 0.5f, 1e-5f, "halfway gives 0.5");

    // Beyond maxDistance it stays at zero instead of going negative.
    ASSERT_NEAR(distanceAttenuation(50.0f,   minD, maxD), 0.0f, 1e-5f, "silent well beyond maxDistance");
    ASSERT_NEAR(distanceAttenuation(1000.0f, minD, maxD), 0.0f, 1e-5f, "still silent much further out");

    // Monotonically decreasing across the falloff band.
    float previous = 1.1f;
    for (float d = minD; d <= maxD; d += 0.5f) {
        const float a = distanceAttenuation(d, minD, maxD);
        ASSERT(a <= previous + 1e-6f, "attenuation never increases with distance");
        ASSERT(a >= 0.0f && a <= 1.0f, "attenuation stays in [0,1]");
        previous = a;
    }

    // Degenerate range: maxDistance <= minDistance is a cliff, not a divide by zero.
    ASSERT_NEAR(distanceAttenuation(1.0f, 5.0f, 5.0f),  1.0f, 1e-5f, "inside a zero-width band");
    ASSERT_NEAR(distanceAttenuation(9.0f, 5.0f, 5.0f),  0.0f, 1e-5f, "outside a zero-width band");
    ASSERT_NEAR(distanceAttenuation(9.0f, 5.0f, 1.0f),  0.0f, 1e-5f, "inverted band is silent outside");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: stereo panning
// ─────────────────────────────────────────────────────────────────────────────
static void test_stereo_panning()
{
    std::printf("  test_stereo_panning\n");

    const Vec3 listenerPos{0.0f, 0.0f, 0.0f};
    const Vec3 forward{0.0f, 0.0f, -1.0f};   // default: looking down -Z

    const float panLeft   = panForEmitter(listenerPos, forward, Vec3{-5.0f, 0.0f, 0.0f});
    const float panRight  = panForEmitter(listenerPos, forward, Vec3{ 5.0f, 0.0f, 0.0f});
    const float panCentre = panForEmitter(listenerPos, forward, Vec3{ 0.0f, 0.0f, -5.0f});

    ASSERT(panLeft  < 0.0f, "emitter on the left yields a negative pan");
    ASSERT(panRight > 0.0f, "emitter on the right yields a positive pan");
    ASSERT_NEAR(panCentre, 0.0f, 1e-5f, "emitter straight ahead is centred");

    const StereoGain gLeft   = stereoGains(panLeft);
    const StereoGain gRight  = stereoGains(panRight);
    const StereoGain gCentre = stereoGains(panCentre);

    ASSERT(gLeft.left  > gLeft.right,  "left emitter is louder in the left channel");
    ASSERT(gRight.right > gRight.left, "right emitter is louder in the right channel");
    ASSERT_NEAR(gCentre.left, gCentre.right, 1e-5f, "centred emitter has equal channel gains");

    // Hard extremes fully mute the opposite channel.
    ASSERT_NEAR(stereoGains(-1.0f).right, 0.0f, 1e-5f, "hard left mutes the right channel");
    ASSERT_NEAR(stereoGains(-1.0f).left,  1.0f, 1e-5f, "hard left keeps the left channel");
    ASSERT_NEAR(stereoGains( 1.0f).left,  0.0f, 1e-5f, "hard right mutes the left channel");
    ASSERT_NEAR(stereoGains( 1.0f).right, 1.0f, 1e-5f, "hard right keeps the right channel");

    // Out-of-range pan values are clamped rather than inverting the image.
    ASSERT_NEAR(stereoGains(-4.0f).right, 0.0f, 1e-5f, "pan below -1 clamps");
    ASSERT_NEAR(stereoGains( 4.0f).left,  0.0f, 1e-5f, "pan above +1 clamps");

    // Rotating the listener 180° swaps the stereo image.
    const Vec3 backwards{0.0f, 0.0f, 1.0f};
    const float panFlipped = panForEmitter(listenerPos, backwards, Vec3{-5.0f, 0.0f, 0.0f});
    ASSERT(panFlipped > 0.0f, "turning around moves a left emitter to the right");

    // An emitter sitting exactly on the listener has no direction to pan towards.
    ASSERT_NEAR(panForEmitter(listenerPos, forward, listenerPos), 0.0f, 1e-5f, "co-located emitter is centred");

    // Looking straight up has no meaningful right axis; the fallback keeps it sane.
    const float panUp = panForEmitter(listenerPos, Vec3{0.0f, 1.0f, 0.0f}, Vec3{5.0f, 0.0f, 0.0f});
    ASSERT(panUp >= -1.0f && panUp <= 1.0f, "degenerate forward still yields a valid pan");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: non-spatial sounds ignore the listener
// ─────────────────────────────────────────────────────────────────────────────
static void test_non_spatial_ignores_distance()
{
    std::printf("  test_non_spatial_ignores_distance\n");

    ListenerState listener;   // at the origin, looking down -Z

    EmitterParams flat;
    flat.spatial     = false;
    flat.volume      = 0.8f;
    flat.minDistance = 1.0f;
    flat.maxDistance = 5.0f;

    flat.position = Vec3{0.0f, 0.0f, 0.0f};
    const SpatialGain near = computeGain(flat, listener, 1.0f, 1.0f);

    flat.position = Vec3{9999.0f, 0.0f, 0.0f};
    const SpatialGain far = computeGain(flat, listener, 1.0f, 1.0f);

    ASSERT_NEAR(far.volume, near.volume, 1e-5f, "non-spatial volume ignores distance");
    ASSERT_NEAR(far.volume, 0.8f,        1e-5f, "non-spatial volume is the raw sound volume");
    ASSERT_NEAR(far.pan,    0.0f,        1e-5f, "non-spatial sounds stay centred");

    // The same emitter with spatial = true is silenced at that distance.
    flat.spatial = true;
    ASSERT_NEAR(computeGain(flat, listener, 1.0f, 1.0f).volume, 0.0f, 1e-5f,
                "spatial version is silent far away");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: sound × bus × master volume mixing
// ─────────────────────────────────────────────────────────────────────────────
static void test_volume_mixing()
{
    std::printf("  test_volume_mixing\n");

    ASSERT_NEAR(mixVolume(0.5f, 0.5f, 0.5f), 0.125f, 1e-5f, "three levels multiply");
    ASSERT_NEAR(mixVolume(1.0f, 1.0f, 1.0f), 1.0f,   1e-5f, "unity everywhere is unity");
    ASSERT_NEAR(mixVolume(1.0f, 0.0f, 1.0f), 0.0f,   1e-5f, "a muted bus mutes the sound");
    ASSERT_NEAR(mixVolume(1.0f, 1.0f, 0.0f), 0.0f,   1e-5f, "a muted master mutes the sound");
    ASSERT_NEAR(mixVolume(-2.0f, 1.0f, 1.0f), 0.0f,  1e-5f, "negative sound volume clamps to silence");

    // Bus routing: Master (0) is unaffected by the sfx/music sliders.
    ASSERT_NEAR(busGain(0, 0.2f, 0.3f), 1.0f, 1e-5f, "bus 0 is Master");
    ASSERT_NEAR(busGain(1, 0.2f, 0.3f), 0.2f, 1e-5f, "bus 1 is Sfx");
    ASSERT_NEAR(busGain(2, 0.2f, 0.3f), 0.3f, 1e-5f, "bus 2 is Music");
    ASSERT_NEAR(busGain(7, 0.2f, 0.3f), 1.0f, 1e-5f, "unknown bus falls back to Master");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 5: AudioEngine::gainFor without a device (headless-safe)
// ─────────────────────────────────────────────────────────────────────────────
static void test_engine_gain_without_device()
{
    std::printf("  test_engine_gain_without_device\n");

    AudioEngine engine;   // deliberately not init()'d
    engine.setMasterVolume(0.5f);
    engine.setSfxVolume(0.5f);
    engine.setMusicVolume(0.25f);
    engine.setListener(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f);

    AudioEngine::PlayParams sfx;
    sfx.volume      = 0.5f;
    sfx.bus         = 1;      // Sfx
    sfx.spatial     = false;
    ASSERT_NEAR(engine.gainFor(sfx).volume, 0.125f, 1e-5f, "sfx voice folds sound × sfx × master");

    AudioEngine::PlayParams music = sfx;
    music.bus = 2;            // Music
    ASSERT_NEAR(engine.gainFor(music).volume, 0.0625f, 1e-5f, "music voice folds sound × music × master");

    AudioEngine::PlayParams master = sfx;
    master.bus = 0;           // Master
    ASSERT_NEAR(engine.gainFor(master).volume, 0.25f, 1e-5f, "master voice skips the sub-bus");

    // Spatial voice at the halfway point loses half of the mixed volume.
    AudioEngine::PlayParams spatial;
    spatial.volume      = 1.0f;
    spatial.bus         = 0;
    spatial.spatial     = true;
    spatial.minDistance = 2.0f;
    spatial.maxDistance = 10.0f;
    spatial.x = 6.0f;
    const dash::audio::SpatialGain g = engine.gainFor(spatial);
    ASSERT_NEAR(g.volume, 0.25f, 1e-5f, "halfway attenuation times master");
    ASSERT_NEAR(g.pan,    1.0f,  1e-5f, "emitter on the +X axis pans hard right");

    // Moving the listener onto the emitter restores full volume.
    engine.setListener(6.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f);
    ASSERT_NEAR(engine.gainFor(spatial).volume, 0.5f, 1e-5f, "listener on top of the emitter hears it fully");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 6: AudioComponent → PlayParams bridge
// ─────────────────────────────────────────────────────────────────────────────
static void test_audio_component_bridge()
{
    std::printf("  test_audio_component_bridge\n");

    AudioComponent comp;
    comp.volume      = 0.6f;
    comp.pitch       = 1.5f;
    comp.loop        = true;
    comp.spatial     = true;
    comp.minDistance = 3.0f;
    comp.maxDistance = 12.0f;
    comp.bus         = static_cast<int>(AudioBus::Music);

    const AudioEngine::PlayParams p = audioPlayParamsFrom(comp, 1.0f, 2.0f, 3.0f);
    ASSERT_NEAR(p.volume, 0.6f, 1e-5f, "volume carried over");
    ASSERT_NEAR(p.pitch,  1.5f, 1e-5f, "pitch carried over");
    ASSERT(p.loop,    "loop carried over");
    ASSERT(p.spatial, "spatial carried over");
    ASSERT_NEAR(p.minDistance, 3.0f,  1e-5f, "minDistance carried over");
    ASSERT_NEAR(p.maxDistance, 12.0f, 1e-5f, "maxDistance carried over");
    ASSERT(p.bus == static_cast<int>(AudioBus::Music), "bus carried over");
    ASSERT_NEAR(p.x, 1.0f, 1e-5f, "x position carried over");
    ASSERT_NEAR(p.y, 2.0f, 1e-5f, "y position carried over");
    ASSERT_NEAR(p.z, 3.0f, 1e-5f, "z position carried over");

    AudioEngine engine;
    engine.setListener(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f);
    engine.setMusicVolume(0.5f);
    // Distance is sqrt(14) ≈ 3.742, just past minDistance, so it is slightly attenuated.
    const float volume = engine.gainFor(p).volume;
    ASSERT(volume > 0.0f && volume < 0.3f, "component-driven voice is attenuated but audible");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 7: every playback entry point is a safe no-op without a device
// ─────────────────────────────────────────────────────────────────────────────
static void test_playback_is_noop_without_device()
{
    std::printf("  test_playback_is_noop_without_device\n");

    AudioEngine engine;   // no init()
    ASSERT(!engine.isInitialized(), "engine reports itself uninitialised");
    ASSERT(engine.loadSound("does_not_exist.wav") == AudioEngine::kInvalidSound, "loadSound returns an invalid handle");
    ASSERT(engine.loadedSoundCount() == 0, "nothing cached");
    ASSERT(!engine.isSoundLoaded("does_not_exist.wav"), "cache lookup is safe");
    ASSERT(engine.playSound(1) == AudioEngine::kInvalidVoice, "playSound returns an invalid voice");
    ASSERT(engine.activeVoiceCount() == 0, "no voices active");
    ASSERT(!engine.isVoicePlaying(1), "no voice is playing");

    engine.stopVoice(1);
    engine.stopAllVoices();
    engine.setVoicePosition(1, 1.0f, 2.0f, 3.0f);
    engine.unloadAllSounds();
    engine.update();
    engine.playTone(440.0f, 10.0f, 0.1f);
    ASSERT(true, "no-op calls did not crash");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 8: real file playback (soft-skips when there is no audio device)
// ─────────────────────────────────────────────────────────────────────────────

// Minimal 16-bit PCM stereo WAV so the test does not depend on repo assets.
static bool writeTestWav(const std::string& path, int frames)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    const std::uint32_t sampleRate = 44100;
    const std::uint16_t channels   = 2;
    const std::uint16_t bits       = 16;
    const std::uint32_t dataBytes  = static_cast<std::uint32_t>(frames) * channels * (bits / 8);
    const std::uint32_t byteRate   = sampleRate * channels * (bits / 8);
    const std::uint16_t blockAlign = static_cast<std::uint16_t>(channels * (bits / 8));

    auto u32 = [&out](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&out](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };

    out.write("RIFF", 4);   u32(36 + dataBytes);   out.write("WAVE", 4);
    out.write("fmt ", 4);   u32(16);
    u16(1);                 u16(channels);         u32(sampleRate);
    u32(byteRate);          u16(blockAlign);       u16(bits);
    out.write("data", 4);   u32(dataBytes);

    for (int i = 0; i < frames; ++i) {
        const auto s = static_cast<std::int16_t>(8000.0 * std::sin(i * 0.05));
        u16(static_cast<std::uint16_t>(s));
        u16(static_cast<std::uint16_t>(s));
    }
    return out.good();
}

static void test_file_playback()
{
    std::printf("  test_file_playback\n");

    AudioEngine engine;
    if (!engine.init()) {
        std::printf("    [SKIP] no audio device — soft pass\n");
        ++g_pass;
        return;
    }
    engine.setMasterVolume(0.0f);   // exercise the code paths without making noise

    const std::string wav = "test_audio_spatial_tmp.wav";
    if (!writeTestWav(wav, 44100)) {
        std::printf("    [SKIP] could not write the temporary WAV — soft pass\n");
        ++g_pass;
        engine.shutdown();
        return;
    }

    const AudioEngine::SoundHandle sound = engine.loadSound(wav);
    ASSERT(sound != AudioEngine::kInvalidSound, "WAV decoded into a valid handle");
    ASSERT(engine.isSoundLoaded(wav), "path registered in the cache");
    ASSERT(engine.loadedSoundCount() == 1, "one entry cached");

    // Same path again must hit the cache instead of decoding a second copy.
    ASSERT(engine.loadSound(wav) == sound, "second load returns the cached handle");
    ASSERT(engine.loadedSoundCount() == 1, "cache did not grow");

    ASSERT(engine.loadSound("nope_missing_file.wav") == AudioEngine::kInvalidSound, "missing file fails cleanly");

    AudioEngine::PlayParams params;
    params.spatial = true;
    params.x = 3.0f;
    const AudioEngine::VoiceHandle voice = engine.playSound(sound, params);
    ASSERT(voice != AudioEngine::kInvalidVoice, "playback returned a voice handle");
    ASSERT(engine.activeVoiceCount() == 1, "one voice active");

    engine.setVoicePosition(voice, -3.0f, 0.0f, 0.0f);
    engine.stopVoice(voice);
    ASSERT(engine.activeVoiceCount() == 0, "stopping the voice frees the slot");
    ASSERT(!engine.isVoicePlaying(voice), "stale handle is not playing");

    // Voice cap: non-looping voices are stolen oldest-first, so the pool never overflows.
    for (int i = 0; i < AudioEngine::kMaxVoices + 8; ++i)
        engine.playSound(sound);
    ASSERT(engine.activeVoiceCount() == AudioEngine::kMaxVoices, "voice count is capped");

    // A pool made entirely of looping voices rejects further requests.
    engine.stopAllVoices();
    AudioEngine::PlayParams looping;
    looping.loop = true;
    for (int i = 0; i < AudioEngine::kMaxVoices; ++i)
        ASSERT(engine.playSound(sound, looping) != AudioEngine::kInvalidVoice, "looping voice started");
    ASSERT(engine.playSound(sound) == AudioEngine::kInvalidVoice, "request dropped when all voices loop");

    engine.stopAllVoices();
    ASSERT(engine.activeVoiceCount() == 0, "stopAllVoices clears the pool");

    engine.unloadAllSounds();
    ASSERT(engine.loadedSoundCount() == 0, "cache emptied");

    engine.shutdown();
    std::remove(wav.c_str());
}

int main()
{
    std::printf("=== Spatial audio tests ===\n");

    test_distance_attenuation();
    test_stereo_panning();
    test_non_spatial_ignores_distance();
    test_volume_mixing();
    test_engine_gain_without_device();
    test_audio_component_bridge();
    test_playback_is_noop_without_device();
    test_file_playback();

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
