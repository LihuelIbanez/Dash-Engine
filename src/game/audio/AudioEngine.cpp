#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "AudioEngine.h"
#include <cstdio>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {
// Everything is decoded to stereo f32 so the balance panner always has two channels.
constexpr ma_uint32 kDecodeChannels = 2;
}  // namespace

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

    // Decoded PCM shared by every voice playing the same file.
    struct SoundData {
        std::vector<float> frames;       // interleaved, kDecodeChannels
        ma_uint64          frameCount = 0;
    };

    struct Voice {
        ma_audio_buffer_ref buffer{};
        ma_sound            sound{};
        bool                active = false;
        VoiceHandle         handle = kInvalidVoice;
        std::uint64_t       order  = 0;   // start ordering, for oldest-first stealing
        PlayParams          params{};
    };

    ma_engine engine{};
    ToneSlot  toneSlots[kMaxTones]{};
    int       nextSlot = 0;

    std::unordered_map<std::string, SoundHandle>  pathToHandle;
    std::vector<std::unique_ptr<SoundData>>       sounds;   // handle == index + 1

    Voice         voices[kMaxVoices]{};
    VoiceHandle   nextVoiceHandle = 1;
    std::uint64_t nextOrder       = 1;

    void stopSlot(ToneSlot& slot)
    {
        if (!slot.active) return;
        ma_sound_uninit(&slot.sound);
        ma_waveform_uninit(&slot.waveform);
        slot.active = false;
    }

    void releaseVoice(Voice& v)
    {
        if (!v.active) return;
        ma_sound_uninit(&v.sound);
        ma_audio_buffer_ref_uninit(&v.buffer);
        v.active = false;
        v.handle = kInvalidVoice;
    }

    Voice* findVoice(VoiceHandle handle)
    {
        if (handle == kInvalidVoice) return nullptr;
        for (auto& v : voices)
            if (v.active && v.handle == handle) return &v;
        return nullptr;
    }

    // Free slot first; otherwise steal the oldest non-looping voice. Looping
    // voices are never stolen, so a fully looping pool rejects new requests.
    Voice* acquireVoice()
    {
        for (auto& v : voices)
            if (!v.active) return &v;

        Voice* oldest = nullptr;
        for (auto& v : voices) {
            if (v.params.loop) continue;
            if (!oldest || v.order < oldest->order) oldest = &v;
        }
        if (!oldest) return nullptr;

        releaseVoice(*oldest);
        return oldest;
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

    for (auto& v : impl_->voices)
        impl_->releaseVoice(v);

    for (auto& slot : impl_->toneSlots)
        impl_->stopSlot(slot);

    ma_engine_uninit(&impl_->engine);
    delete impl_;
    impl_ = nullptr;

    initialized_ = false;
    std::fprintf(stderr, "[AudioEngine] shut down\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// File loading — decode once, cache by path
// ─────────────────────────────────────────────────────────────────────────────

AudioEngine::SoundHandle AudioEngine::loadSound(const std::string& path)
{
    if (!initialized_ || !impl_ || path.empty()) return kInvalidSound;

    auto cached = impl_->pathToHandle.find(path);
    if (cached != impl_->pathToHandle.end()) return cached->second;

    ma_decoder_config decoderCfg = ma_decoder_config_init(
        ma_format_f32, kDecodeChannels, ma_engine_get_sample_rate(&impl_->engine));

    ma_uint64 frameCount = 0;
    void*     pcm        = nullptr;
    if (ma_decode_file(path.c_str(), &decoderCfg, &frameCount, &pcm) != MA_SUCCESS || !pcm) {
        std::fprintf(stderr, "[AudioEngine] could not decode '%s'\n", path.c_str());
        return kInvalidSound;
    }
    if (frameCount == 0) {
        ma_free(pcm, nullptr);
        return kInvalidSound;
    }

    auto data = std::make_unique<Impl::SoundData>();
    const float* src = static_cast<const float*>(pcm);
    data->frames.assign(src, src + frameCount * kDecodeChannels);
    data->frameCount = frameCount;
    ma_free(pcm, nullptr);

    impl_->sounds.push_back(std::move(data));
    const SoundHandle handle = static_cast<SoundHandle>(impl_->sounds.size());
    impl_->pathToHandle.emplace(path, handle);
    return handle;
}

bool AudioEngine::isSoundLoaded(const std::string& path) const
{
    if (!impl_) return false;
    return impl_->pathToHandle.count(path) > 0;
}

int AudioEngine::loadedSoundCount() const
{
    return impl_ ? static_cast<int>(impl_->sounds.size()) : 0;
}

void AudioEngine::unloadAllSounds()
{
    if (!impl_) return;
    stopAllVoices();
    impl_->pathToHandle.clear();
    impl_->sounds.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Voices
// ─────────────────────────────────────────────────────────────────────────────

AudioEngine::VoiceHandle AudioEngine::playSound(SoundHandle sound, const PlayParams& params)
{
    if (!initialized_ || !impl_) return kInvalidVoice;
    if (sound == kInvalidSound || sound > impl_->sounds.size()) return kInvalidVoice;

    Impl::SoundData& data = *impl_->sounds[sound - 1];
    if (data.frameCount == 0) return kInvalidVoice;

    Impl::Voice* voice = impl_->acquireVoice();
    if (!voice) return kInvalidVoice;

    if (ma_audio_buffer_ref_init(ma_format_f32, kDecodeChannels,
                                 data.frames.data(), data.frameCount, &voice->buffer) != MA_SUCCESS)
        return kInvalidVoice;
    voice->buffer.sampleRate = ma_engine_get_sample_rate(&impl_->engine);  // ref_init leaves this at 0

    // Spatialisation is computed here, not by miniaudio, so both match the unit tests.
    if (ma_sound_init_from_data_source(&impl_->engine, &voice->buffer,
                                       MA_SOUND_FLAG_NO_SPATIALIZATION,
                                       nullptr, &voice->sound) != MA_SUCCESS) {
        ma_audio_buffer_ref_uninit(&voice->buffer);
        return kInvalidVoice;
    }

    voice->active = true;
    voice->params = params;
    voice->handle = impl_->nextVoiceHandle++;
    voice->order  = impl_->nextOrder++;
    if (impl_->nextVoiceHandle == kInvalidVoice) impl_->nextVoiceHandle = 1;

    ma_sound_set_looping(&voice->sound, params.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_pitch(&voice->sound, std::max(0.01f, params.pitch));

    const dash::audio::SpatialGain gain = gainFor(params);
    ma_sound_set_volume(&voice->sound, gain.volume);
    ma_sound_set_pan(&voice->sound, gain.pan);

    ma_sound_start(&voice->sound);
    return voice->handle;
}

void AudioEngine::stopVoice(VoiceHandle voice)
{
    if (!initialized_ || !impl_) return;
    if (Impl::Voice* v = impl_->findVoice(voice))
        impl_->releaseVoice(*v);
}

void AudioEngine::stopAllVoices()
{
    if (!impl_) return;
    for (auto& v : impl_->voices)
        impl_->releaseVoice(v);
}

bool AudioEngine::isVoicePlaying(VoiceHandle voice) const
{
    if (!initialized_ || !impl_) return false;
    const Impl::Voice* v = impl_->findVoice(voice);
    return v && ma_sound_is_playing(&v->sound);
}

int AudioEngine::activeVoiceCount() const
{
    if (!impl_) return 0;
    int n = 0;
    for (const auto& v : impl_->voices)
        if (v.active) ++n;
    return n;
}

// ─────────────────────────────────────────────────────────────────────────────
// Spatialisation
// ─────────────────────────────────────────────────────────────────────────────

void AudioEngine::setListener(float x, float y, float z, float forwardX, float forwardY, float forwardZ)
{
    listener_.position = {x, y, z};
    listener_.forward  = {forwardX, forwardY, forwardZ};
    applyAllVoiceGains();
}

void AudioEngine::setVoicePosition(VoiceHandle voice, float x, float y, float z)
{
    if (!initialized_ || !impl_) return;
    Impl::Voice* v = impl_->findVoice(voice);
    if (!v) return;

    v->params.x = x;
    v->params.y = y;
    v->params.z = z;

    const dash::audio::SpatialGain gain = gainFor(v->params);
    ma_sound_set_volume(&v->sound, gain.volume);
    ma_sound_set_pan(&v->sound, gain.pan);
}

dash::audio::SpatialGain AudioEngine::gainFor(const PlayParams& params) const
{
    dash::audio::EmitterParams emitter;
    emitter.position    = {params.x, params.y, params.z};
    emitter.volume      = params.volume;
    emitter.spatial     = params.spatial;
    emitter.minDistance = params.minDistance;
    emitter.maxDistance = params.maxDistance;

    return dash::audio::computeGain(
        emitter, listener_,
        dash::audio::busGain(params.bus, sfxVolume_, musicVolume_),
        masterVolume_);
}

void AudioEngine::update()
{
    if (!initialized_ || !impl_) return;

    for (auto& v : impl_->voices) {
        if (!v.active) continue;
        if (!ma_sound_is_playing(&v.sound) || ma_sound_at_end(&v.sound)) {
            impl_->releaseVoice(v);
            continue;
        }
        const dash::audio::SpatialGain gain = gainFor(v.params);
        ma_sound_set_volume(&v.sound, gain.volume);
        ma_sound_set_pan(&v.sound, gain.pan);
    }
}

void AudioEngine::applyAllVoiceGains()
{
    if (!initialized_ || !impl_) return;
    for (auto& v : impl_->voices) {
        if (!v.active) continue;
        const dash::audio::SpatialGain gain = gainFor(v.params);
        ma_sound_set_volume(&v.sound, gain.volume);
        ma_sound_set_pan(&v.sound, gain.pan);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Tones and volumes
// ─────────────────────────────────────────────────────────────────────────────

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
    // Master is folded into each voice's gain instead of ma_engine_set_volume,
    // otherwise it would be applied twice (tones already bake it in).
    masterVolume_ = std::clamp(v, 0.0f, 1.0f);
    applyAllVoiceGains();
}

void AudioEngine::setSfxVolume(float v)
{
    sfxVolume_ = std::clamp(v, 0.0f, 1.0f);
    applyAllVoiceGains();
}

void AudioEngine::setMusicVolume(float v)
{
    musicVolume_ = std::clamp(v, 0.0f, 1.0f);
    applyAllVoiceGains();
}
