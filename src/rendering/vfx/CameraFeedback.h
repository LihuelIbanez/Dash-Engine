#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// CameraFeedback — what the player feels when they get hit.
//
// ScreenShake is trauma based (Jonas Tyroller / Squirrel Eiserloh): hits add
// trauma, trauma decays linearly and the offset is trauma², so small hits are
// almost invisible and big ones are not. Shaking the camera and not the image
// keeps parallax honest — a shader-space wobble would slide the sky against the
// terrain.
//
// HitFlash is the other half: a tint the tonemap pass folds in before ACES.
// Both are pure scalars, so they are unit tested with the particle pool.
// ─────────────────────────────────────────────────────────────────────────────

namespace dash::vfx {

class ScreenShake {
public:
    void addTrauma(float amount)
    {
        trauma_ = std::min(1.0f, trauma_ + std::max(0.f, amount));
    }

    void update(float dt)
    {
        if (dt <= 0.f) return;
        time_ += dt;
        trauma_ = std::max(0.f, trauma_ - decayPerSecond_ * dt);
    }

    void reset() { trauma_ = 0.f; }

    float trauma() const { return trauma_; }
    bool active() const { return trauma_ > 0.001f; }

    // World-space camera offset. Three decorrelated sine sums stand in for
    // Perlin noise: cheap, deterministic, and no visible period at these rates.
    void offset(float magnitude, float outXyz[3]) const
    {
        const float k = trauma_ * trauma_ * magnitude;
        outXyz[0] = k * wave(13.7f, 0.0f);
        outXyz[1] = k * wave(17.3f, 2.1f);
        outXyz[2] = k * wave(11.1f, 4.7f);
    }

    void setDecay(float perSecond) { decayPerSecond_ = std::max(0.01f, perSecond); }

private:
    float wave(float rate, float phase) const
    {
        const float t = time_ * rate + phase;
        return 0.62f * std::sin(t) + 0.38f * std::sin(t * 2.37f + 1.3f);
    }

    float trauma_ = 0.f;
    float time_ = 0.f;
    float decayPerSecond_ = 2.6f;
};

class HitFlash {
public:
    void trigger(float strength)
    {
        strength_ = std::min(maxStrength_, std::max(strength_, std::max(0.f, strength)));
    }

    void update(float dt)
    {
        if (dt <= 0.f) return;
        strength_ = std::max(0.f, strength_ - decayPerSecond_ * dt);
    }

    void reset() { strength_ = 0.f; }

    float strength() const { return strength_; }
    bool active() const { return strength_ > 0.001f; }

    // Premultiplied tint for the three spare floats of the tonemap push
    // constants (lift.w / gammaC.w / gain.w). The shader recovers the weight as
    // the max component, so `tint` must peak at 1.
    void packPremultiplied(float outRgb[3]) const
    {
        outRgb[0] = tint_[0] * strength_;
        outRgb[1] = tint_[1] * strength_;
        outRgb[2] = tint_[2] * strength_;
    }

    void setTint(float r, float g, float b) { tint_[0] = r; tint_[1] = g; tint_[2] = b; }

private:
    float strength_ = 0.f;
    float decayPerSecond_ = 3.2f;
    float maxStrength_ = 0.80f;
    // Deep arterial red; the max component is 1 by construction.
    float tint_[3] = {1.0f, 0.10f, 0.07f};
};

} // namespace dash::vfx
