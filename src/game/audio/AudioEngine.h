#pragma once

#include <cstdint>
#include <string>

#include "AudioMath.h"

// ─────────────────────────────────────────────────────────────────────────────
// AudioEngine — centralised audio backend powered by miniaudio.
// Plays decoded audio files (WAV / MP3 / FLAC) and procedural tones through
// three volume buses, with linear distance attenuation and stereo panning.
// All methods are no-ops when audio initialisation fails (headless CI, etc.).
// ─────────────────────────────────────────────────────────────────────────────

// How a single playing instance should sound.
struct AudioPlayParams {
    float volume      = 1.0f;
    float pitch       = 1.0f;
    bool  loop        = false;
    bool  spatial     = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float minDistance = 1.0f;
    float maxDistance = 20.0f;
    int   bus = 1;   // AudioBus: 0 Master, 1 Sfx, 2 Music
};

class AudioEngine {
public:
    using SoundHandle = std::uint32_t;   // 0 = invalid
    using VoiceHandle = std::uint32_t;   // 0 = invalid
    using PlayParams  = AudioPlayParams;

    static constexpr SoundHandle kInvalidSound = 0;
    static constexpr VoiceHandle kInvalidVoice = 0;

    // Simultaneous playing instances. When every slot is busy a new request
    // steals the oldest non-looping voice; if all voices are looping the
    // request is dropped and playSound() returns kInvalidVoice.
    static constexpr int kMaxVoices = 32;

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool init();
    void shutdown();

    // ── File playback ────────────────────────────────────────────────────────
    // Decodes the file once and caches the PCM by path; repeated calls with the
    // same path return the same handle without touching the disk again.
    SoundHandle loadSound(const std::string& path);
    bool        isSoundLoaded(const std::string& path) const;
    int         loadedSoundCount() const;
    void        unloadAllSounds();

    VoiceHandle playSound(SoundHandle sound, const PlayParams& params = {});
    void        stopVoice(VoiceHandle voice);
    void        stopAllVoices();
    bool        isVoicePlaying(VoiceHandle voice) const;
    int         activeVoiceCount() const;

    // ── 3D listener ──────────────────────────────────────────────────────────
    void setListener(float x, float y, float z, float forwardX, float forwardY, float forwardZ);
    const dash::audio::ListenerState& listener() const { return listener_; }

    void setVoicePosition(VoiceHandle voice, float x, float y, float z);

    // Reaps finished voices and re-applies spatial gains. Call once per frame.
    void update();

    // Volume/pan a voice would get right now. Pure — usable without a device.
    dash::audio::SpatialGain gainFor(const PlayParams& params) const;

    // Play a procedural sine-wave tone at the given frequency / duration.
    void playTone(float freqHz, float durationMs, float amplitude = 0.3f);

    // Volume controls (0.0 – 1.0).
    void  setMasterVolume(float v);
    void  setSfxVolume(float v);
    void  setMusicVolume(float v);
    float masterVolume() const { return masterVolume_; }
    float sfxVolume()    const { return sfxVolume_; }
    float musicVolume()  const { return musicVolume_; }

    bool isInitialized() const { return initialized_; }

private:
    struct Impl;         // pimpl — hides miniaudio types from the header
    Impl* impl_ = nullptr;
    bool  initialized_ = false;

    void applyAllVoiceGains();

    dash::audio::ListenerState listener_{};

    float masterVolume_ = 1.0f;
    float sfxVolume_    = 1.0f;
    float musicVolume_  = 1.0f;
};
