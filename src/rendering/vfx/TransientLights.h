#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// TransientLights — short-lived point lights for impacts and spells.
//
// The scene light array the shaders read is a fixed kMaxSceneLights block, so
// this pool never owns a slot: it only *offers* candidates. The renderer copies
// the authored scene lights first (keeping their indices, which the shadow map
// depends on) and fills whatever is left with the best-scoring flashes.
//
// Score is peakIntensity / (1 + distance to camera)²: a bright flash next to
// the camera beats a dim one across the map, which is the only thing that
// matters when the budget is eight lights.
// ─────────────────────────────────────────────────────────────────────────────

namespace dash::vfx {

struct TransientLight {
    float x = 0.f, y = 0.f, z = 0.f;
    float r = 1.f, g = 1.f, b = 1.f;
    float peakIntensity = 6.f;
    float range = 4.f;
    float age = 0.f;
    float life = 0.2f;
};

class TransientLights {
public:
    explicit TransientLights(std::size_t capacity = 32) : capacity_(capacity)
    {
        lights_.reserve(capacity_);
    }

    // Spawns a flash. When the pool is full the oldest entry is recycled, so a
    // burst of deaths never drops the most recent (most visible) flash.
    void spawn(const TransientLight& light)
    {
        TransientLight l = light;
        l.age = 0.f;
        if (l.life <= 0.f) l.life = 0.01f;

        if (lights_.size() < capacity_) {
            lights_.push_back(l);
            ++totalSpawned_;
            return;
        }
        std::size_t oldest = 0;
        float best = -1.f;
        for (std::size_t i = 0; i < lights_.size(); ++i) {
            const float t = lights_[i].age / lights_[i].life;
            if (t > best) { best = t; oldest = i; }
        }
        lights_[oldest] = l;
        ++totalSpawned_;
    }

    void update(float dt)
    {
        if (dt <= 0.f) return;
        std::size_t i = 0;
        while (i < lights_.size()) {
            lights_[i].age += dt;
            if (lights_[i].age >= lights_[i].life) {
                lights_[i] = lights_.back();
                lights_.pop_back();
                continue;
            }
            ++i;
        }
    }

    void clear() { lights_.clear(); }

    // Quadratic falloff: the flash is at full power on the first frame and is
    // already half gone a third of the way through, which reads as a "pop".
    static float currentIntensity(const TransientLight& l)
    {
        const float t = std::min(1.f, l.age / l.life);
        const float k = 1.f - t;
        return l.peakIntensity * k * k;
    }

    static float score(const TransientLight& l, float camX, float camY, float camZ)
    {
        const float dx = l.x - camX, dy = l.y - camY, dz = l.z - camZ;
        const float d2 = dx * dx + dy * dy + dz * dz;
        return currentIntensity(l) / (1.f + d2);
    }

    // Writes at most `maxCount` indices into `outIndices`, best score first.
    std::size_t select(float camX, float camY, float camZ, std::size_t maxCount,
                       std::vector<std::size_t>& outIndices) const
    {
        outIndices.clear();
        if (maxCount == 0 || lights_.empty()) return 0;

        outIndices.reserve(std::min(maxCount, lights_.size()));
        for (std::size_t i = 0; i < lights_.size(); ++i) outIndices.push_back(i);

        std::sort(outIndices.begin(), outIndices.end(),
                  [&](std::size_t a, std::size_t b) {
                      return score(lights_[a], camX, camY, camZ)
                           > score(lights_[b], camX, camY, camZ);
                  });
        if (outIndices.size() > maxCount) outIndices.resize(maxCount);
        return outIndices.size();
    }

    const std::vector<TransientLight>& lights() const { return lights_; }
    std::size_t liveCount() const { return lights_.size(); }
    std::size_t capacity() const { return capacity_; }
    unsigned long long totalSpawned() const { return totalSpawned_; }

private:
    std::vector<TransientLight> lights_;
    std::size_t capacity_;
    unsigned long long totalSpawned_ = 0;
};

} // namespace dash::vfx
