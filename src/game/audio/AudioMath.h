#pragma once

#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// AudioMath — pure helpers behind spatialisation and volume mixing.
// Deliberately free of miniaudio so they can be unit-tested on a headless CI
// box where AudioEngine::init() fails and every engine method is a no-op.
// ─────────────────────────────────────────────────────────────────────────────

namespace dash::audio {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct StereoGain {
    float left  = 1.0f;
    float right = 1.0f;
};

struct ListenerState {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 forward{0.0f, 0.0f, -1.0f};
};

struct EmitterParams {
    Vec3  position{};
    float volume      = 1.0f;
    bool  spatial     = true;
    float minDistance = 1.0f;
    float maxDistance = 20.0f;
};

struct SpatialGain {
    float volume = 1.0f;   // sound × bus × master × distance attenuation
    float pan    = 0.0f;   // -1 hard left … 0 centre … +1 hard right
};

inline float distanceBetween(const Vec3& a, const Vec3& b)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dz = b.z - a.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// 1.0 at or inside minDistance, 0.0 at or beyond maxDistance, linear between.
inline float distanceAttenuation(float distance, float minDistance, float maxDistance)
{
    minDistance = std::max(0.0f, minDistance);
    if (distance <= minDistance) return 1.0f;
    if (maxDistance <= minDistance) return 0.0f;   // degenerate range: cliff at minDistance
    if (distance >= maxDistance) return 0.0f;
    return 1.0f - (distance - minDistance) / (maxDistance - minDistance);
}

// Balance pan law, matching ma_pan_mode_balance so the maths mirror what is heard.
inline StereoGain stereoGains(float pan)
{
    pan = std::clamp(pan, -1.0f, 1.0f);
    StereoGain g;
    if (pan > 0.0f) g.left  = 1.0f - pan;
    else            g.right = 1.0f + pan;
    return g;
}

// Projects the listener→emitter direction onto the listener's right axis.
inline float panForEmitter(const Vec3& listenerPos, const Vec3& listenerForward, const Vec3& emitterPos)
{
    // right = forward × worldUp(0,1,0)
    float rx = -listenerForward.z;
    float ry = 0.0f;
    float rz = listenerForward.x;
    float rLen = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (rLen < 1e-6f) {   // looking straight up or down: no meaningful right axis
        rx = 1.0f; ry = 0.0f; rz = 0.0f; rLen = 1.0f;
    }
    rx /= rLen; ry /= rLen; rz /= rLen;

    const float dx = emitterPos.x - listenerPos.x;
    const float dy = emitterPos.y - listenerPos.y;
    const float dz = emitterPos.z - listenerPos.z;
    const float dLen = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dLen < 1e-6f) return 0.0f;

    return std::clamp((dx * rx + dy * ry + dz * rz) / dLen, -1.0f, 1.0f);
}

// Final linear gain of a voice before spatialisation.
inline float mixVolume(float soundVolume, float busVolume, float masterVolume)
{
    return std::max(0.0f, soundVolume)
         * std::clamp(busVolume,    0.0f, 1.0f)
         * std::clamp(masterVolume, 0.0f, 1.0f);
}

// Master is applied on top of every bus, so bus 0 (Master) contributes 1.0 here.
inline float busGain(int bus, float sfxVolume, float musicVolume)
{
    switch (bus) {
        case 1:  return sfxVolume;     // AudioBus::Sfx
        case 2:  return musicVolume;   // AudioBus::Music
        default: return 1.0f;          // AudioBus::Master
    }
}

inline SpatialGain computeGain(const EmitterParams& emitter,
                               const ListenerState& listener,
                               float busVolume,
                               float masterVolume)
{
    SpatialGain out;
    out.volume = mixVolume(emitter.volume, busVolume, masterVolume);
    if (!emitter.spatial) return out;   // flat playback ignores the listener entirely

    const float d = distanceBetween(listener.position, emitter.position);
    out.volume *= distanceAttenuation(d, emitter.minDistance, emitter.maxDistance);
    out.pan     = panForEmitter(listener.position, listener.forward, emitter.position);
    return out;
}

}  // namespace dash::audio
