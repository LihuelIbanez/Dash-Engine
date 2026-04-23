#pragma once

#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// AudioEngine — centralised audio backend powered by miniaudio.
// Provides procedural tone playback and volume control.
// All methods are no-ops when audio initialisation fails (headless CI, etc.).
// ─────────────────────────────────────────────────────────────────────────────

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool init();
    void shutdown();

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

    float masterVolume_ = 1.0f;
    float sfxVolume_    = 1.0f;
    float musicVolume_  = 1.0f;
};
