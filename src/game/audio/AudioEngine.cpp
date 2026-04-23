#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "AudioEngine.h"
#include <cstdio>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Pimpl — all miniaudio types are hidden here so the header stays clean.
// ─────────────────────────────────────────────────────────────────────────────
struct AudioEngine::Impl {
    static constexpr int kMaxTones = 8;

    struct ToneSlot {
        ma_waveform waveform{};
        ma_sound    sound{};
        bool        active = false;
    };

    ma_engine engine{};
    ToneSlot  toneSlots[kMaxTones]{};
    int       nextSlot = 0;

    void stopSlot(ToneSlot& slot)
    {
        if (!slot.active) return;
        ma_sound_uninit(&slot.sound);
        ma_waveform_uninit(&slot.waveform);
        slot.active = false;
    }
};

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    shutdown();
}

bool AudioEngine::init()
{
    if (initialized_) return true;

    impl_ = new(std::nothrow) Impl;
    if (!impl_) return false;

    ma_engine_config cfg = ma_engine_config_init();
    ma_result result = ma_engine_init(&cfg, &impl_->engine);
    if (result != MA_SUCCESS) {
        std::fprintf(stderr, "[AudioEngine] init failed (code %d) — running without audio\n", result);
        delete impl_;
        impl_ = nullptr;
        return false;
    }

    initialized_ = true;
    std::fprintf(stderr, "[AudioEngine] initialised\n");
    return true;
}

void AudioEngine::shutdown()
{
    if (!initialized_ || !impl_) return;

    for (auto& slot : impl_->toneSlots)
        impl_->stopSlot(slot);

    ma_engine_uninit(&impl_->engine);
    delete impl_;
    impl_ = nullptr;

    initialized_ = false;
    std::fprintf(stderr, "[AudioEngine] shut down\n");
}

void AudioEngine::playTone(float freqHz, float durationMs, float amplitude)
{
    if (!initialized_ || !impl_) return;

    auto& slot = impl_->toneSlots[impl_->nextSlot];
    impl_->stopSlot(slot);

    ma_waveform_config wfCfg = ma_waveform_config_init(
        ma_format_f32,
        2,
        ma_engine_get_sample_rate(&impl_->engine),
        ma_waveform_type_sine,
        amplitude * sfxVolume_ * masterVolume_,
        freqHz);

    if (ma_waveform_init(&wfCfg, &slot.waveform) != MA_SUCCESS)
        return;

    if (ma_sound_init_from_data_source(&impl_->engine, &slot.waveform,
            MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION,
            nullptr, &slot.sound) != MA_SUCCESS)
    {
        ma_waveform_uninit(&slot.waveform);
        return;
    }

    ma_sound_set_fade_in_milliseconds(&slot.sound, 1.0f, 0.0f,
                                      static_cast<ma_uint64>(durationMs));
    ma_sound_set_stop_time_in_milliseconds(&slot.sound,
        ma_engine_get_time_in_milliseconds(&impl_->engine) + static_cast<ma_uint64>(durationMs));

    ma_sound_start(&slot.sound);
    slot.active = true;

    impl_->nextSlot = (impl_->nextSlot + 1) % Impl::kMaxTones;
}

void AudioEngine::setMasterVolume(float v)
{
    masterVolume_ = std::clamp(v, 0.0f, 1.0f);
    if (impl_)
        ma_engine_set_volume(&impl_->engine, masterVolume_);
}

void AudioEngine::setSfxVolume(float v)
{
    sfxVolume_ = std::clamp(v, 0.0f, 1.0f);
}

void AudioEngine::setMusicVolume(float v)
{
    musicVolume_ = std::clamp(v, 0.0f, 1.0f);
}
