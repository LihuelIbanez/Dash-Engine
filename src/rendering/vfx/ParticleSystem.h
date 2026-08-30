#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// ParticleSystem — CPU pool for combat VFX.
//
// Deliberately free of Vulkan types: the simulation is a plain array of structs
// so it can be unit tested (tests/test_particle_system.cpp) without a device.
// The renderer only consumes ParticleInstance, which mirrors the per-instance
// vertex layout of assets/shaders/particle.vert one to one.
// ─────────────────────────────────────────────────────────────────────────────

namespace dash::vfx {

enum class BlendMode : int { Alpha = 0, Additive = 1 };

// 16 floats = 64 bytes. Matches the four vec4 vertex attributes of particle.vert.
struct ParticleInstance {
    float centerSize[4]{};  // xyz world position, w half extent
    float color[4]{};       // rgba, straight alpha (the shader premultiplies)
    float uvRect[4]{};      // u0, v0, uSpan, vSpan inside the atlas
    float params[4]{};      // x = rotation in radians, yzw spare
};

struct Particle {
    float px = 0.f, py = 0.f, pz = 0.f;
    float vx = 0.f, vy = 0.f, vz = 0.f;
    float age = 0.f;
    float life = 1.f;
    float sizeBegin = 0.1f, sizeEnd = 0.f;
    float colorBegin[4] = {1.f, 1.f, 1.f, 1.f};
    float colorEnd[4]   = {1.f, 1.f, 1.f, 0.f};
    float gravity = 0.f;    // world units / s², applied to vy
    float drag = 0.f;       // fraction of velocity shed per second
    float rotation = 0.f, spin = 0.f;
    // Ground plane. A particle that reaches it sticks there and finishes its
    // life fading in place, which is what makes blood pool instead of sinking.
    float floorY = -1.0e9f;
    int   frameFirst = 0, frameCount = 1;
    float frameRate = 0.f;  // frames/s; 0 spreads frameCount across the whole life
    BlendMode blend = BlendMode::Alpha;
};

// One burst. Directions are sampled inside a cone around (dirX, dirY, dirZ):
// spread 0 is a laser, 1 a hemisphere, 2 a full sphere.
struct EmitParams {
    float x = 0.f, y = 0.f, z = 0.f;
    float radius = 0.f;                   // jitter added to the spawn point
    float dirX = 0.f, dirY = 1.f, dirZ = 0.f;
    float spread = 1.0f;
    float speedMin = 1.0f, speedMax = 3.0f;
    float lifeMin = 0.3f, lifeMax = 0.7f;
    float sizeBegin = 0.12f, sizeEnd = 0.02f;
    float colorBegin[4] = {1.f, 1.f, 1.f, 1.f};
    float colorEnd[4]   = {1.f, 1.f, 1.f, 0.f};
    float gravity = -9.8f;
    float drag = 1.5f;
    float spinMax = 4.0f;
    float floorY = -1.0e9f;
    int   frameFirst = 0, frameCount = 4;
    float frameRate = 0.f;
    BlendMode blend = BlendMode::Alpha;
    int   count = 12;
};

class ParticleSystem {
public:
    explicit ParticleSystem(std::size_t capacity = 2048) : capacity_(capacity)
    {
        particles_.reserve(capacity_);
    }

    void setSeed(uint32_t seed) { rng_ = seed ? seed : 0x9E3779B9u; }

    void clear()
    {
        particles_.clear();
        totalEmitted_ = 0;
        totalRetired_ = 0;
    }

    // Returns how many particles were actually spawned; the pool never grows
    // past its capacity, so a burst on a full pool is silently truncated.
    int emit(const EmitParams& p)
    {
        const float ax = p.dirX, ay = p.dirY, az = p.dirZ;
        float axis[3] = {ax, ay, az};
        normalize3(axis);
        float b1[3], b2[3];
        basisFrom(axis, b1, b2);

        int spawned = 0;
        for (int i = 0; i < p.count; ++i) {
            if (particles_.size() >= capacity_) break;

            Particle q;
            q.px = p.x + (rand01() * 2.f - 1.f) * p.radius;
            q.py = p.y + (rand01() * 2.f - 1.f) * p.radius;
            q.pz = p.z + (rand01() * 2.f - 1.f) * p.radius;

            const float phi = rand01() * 6.28318530718f;
            const float cosTheta = std::max(-1.f, 1.f - rand01() * p.spread);
            const float sinTheta = std::sqrt(std::max(0.f, 1.f - cosTheta * cosTheta));
            const float cx = std::cos(phi) * sinTheta;
            const float cy = std::sin(phi) * sinTheta;

            const float speed = lerp(p.speedMin, p.speedMax, rand01());
            q.vx = (axis[0] * cosTheta + b1[0] * cx + b2[0] * cy) * speed;
            q.vy = (axis[1] * cosTheta + b1[1] * cx + b2[1] * cy) * speed;
            q.vz = (axis[2] * cosTheta + b1[2] * cx + b2[2] * cy) * speed;

            q.life = std::max(0.01f, lerp(p.lifeMin, p.lifeMax, rand01()));
            q.sizeBegin = p.sizeBegin;
            q.sizeEnd = p.sizeEnd;
            for (int c = 0; c < 4; ++c) {
                q.colorBegin[c] = p.colorBegin[c];
                q.colorEnd[c] = p.colorEnd[c];
            }
            q.gravity = p.gravity;
            q.drag = p.drag;
            q.rotation = rand01() * 6.28318530718f;
            q.spin = (rand01() * 2.f - 1.f) * p.spinMax;
            q.floorY = p.floorY;
            q.frameFirst = p.frameFirst;
            q.frameCount = std::max(1, p.frameCount);
            q.frameRate = p.frameRate;
            q.blend = p.blend;

            particles_.push_back(q);
            ++spawned;
        }
        totalEmitted_ += static_cast<uint64_t>(spawned);
        return spawned;
    }

    void update(float dt)
    {
        if (dt <= 0.f) return;

        std::size_t i = 0;
        while (i < particles_.size()) {
            Particle& q = particles_[i];
            q.age += dt;
            if (q.age >= q.life) {
                particles_[i] = particles_.back();
                particles_.pop_back();
                ++totalRetired_;
                continue;
            }

            q.vy += q.gravity * dt;
            const float damp = std::max(0.f, 1.f - q.drag * dt);
            q.vx *= damp;
            q.vy *= damp;
            q.vz *= damp;

            q.px += q.vx * dt;
            q.py += q.vy * dt;
            q.pz += q.vz * dt;
            q.rotation += q.spin * dt;

            if (q.py < q.floorY) {
                q.py = q.floorY;
                q.vx = q.vy = q.vz = 0.f;
                q.spin = 0.f;
            }
            ++i;
        }
    }

    // Splits the live set into the two blend batches, already in GPU layout.
    // `inset` shrinks each atlas cell so bilinear filtering cannot bleed in the
    // neighbouring frame.
    void buildInstances(std::vector<ParticleInstance>& outAlpha,
                        std::vector<ParticleInstance>& outAdditive,
                        int atlasCols, int atlasRows,
                        float inset = 0.004f) const
    {
        outAlpha.clear();
        outAdditive.clear();
        if (atlasCols < 1) atlasCols = 1;
        if (atlasRows < 1) atlasRows = 1;

        const float cellU = 1.f / static_cast<float>(atlasCols);
        const float cellV = 1.f / static_cast<float>(atlasRows);

        for (const Particle& q : particles_) {
            const float t = std::min(1.f, q.age / q.life);

            ParticleInstance inst;
            inst.centerSize[0] = q.px;
            inst.centerSize[1] = q.py;
            inst.centerSize[2] = q.pz;
            inst.centerSize[3] = lerp(q.sizeBegin, q.sizeEnd, t);
            for (int c = 0; c < 4; ++c) {
                inst.color[c] = lerp(q.colorBegin[c], q.colorEnd[c], t);
            }

            const int frame = atlasFrame(q, t);
            const int cell = frame % (atlasCols * atlasRows);
            const int col = cell % atlasCols;
            const int row = cell / atlasCols;
            inst.uvRect[0] = static_cast<float>(col) * cellU + inset;
            inst.uvRect[1] = static_cast<float>(row) * cellV + inset;
            inst.uvRect[2] = cellU - 2.f * inset;
            inst.uvRect[3] = cellV - 2.f * inset;

            inst.params[0] = q.rotation;

            (q.blend == BlendMode::Additive ? outAdditive : outAlpha).push_back(inst);
        }
    }

    static int atlasFrame(const Particle& q, float t)
    {
        if (q.frameCount <= 1) return q.frameFirst;
        const float f = q.frameRate > 0.f ? q.age * q.frameRate
                                          : t * static_cast<float>(q.frameCount);
        int idx = static_cast<int>(f);
        idx = std::min(idx, q.frameCount - 1);
        idx = std::max(idx, 0);
        return q.frameFirst + idx;
    }

    std::size_t aliveCount() const { return particles_.size(); }
    std::size_t capacity() const { return capacity_; }
    uint64_t totalEmitted() const { return totalEmitted_; }
    uint64_t totalRetired() const { return totalRetired_; }
    const std::vector<Particle>& particles() const { return particles_; }

private:
    static float lerp(float a, float b, float t) { return a + (b - a) * t; }

    static void normalize3(float v[3])
    {
        const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (len < 1e-6f) { v[0] = 0.f; v[1] = 1.f; v[2] = 0.f; return; }
        v[0] /= len; v[1] /= len; v[2] /= len;
    }

    // Any two vectors perpendicular to `axis`; the seed is picked away from it
    // so the cross product never degenerates.
    static void basisFrom(const float axis[3], float b1[3], float b2[3])
    {
        float seed[3] = {0.f, 1.f, 0.f};
        if (std::fabs(axis[1]) > 0.9f) { seed[0] = 1.f; seed[1] = 0.f; }

        b1[0] = axis[1] * seed[2] - axis[2] * seed[1];
        b1[1] = axis[2] * seed[0] - axis[0] * seed[2];
        b1[2] = axis[0] * seed[1] - axis[1] * seed[0];
        normalize3(b1);

        b2[0] = axis[1] * b1[2] - axis[2] * b1[1];
        b2[1] = axis[2] * b1[0] - axis[0] * b1[2];
        b2[2] = axis[0] * b1[1] - axis[1] * b1[0];
        normalize3(b2);
    }

    float rand01()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float>(rng_ & 0x00FFFFFFu) / 16777216.0f;
    }

    std::vector<Particle> particles_;
    std::size_t capacity_;
    uint32_t rng_ = 0x9E3779B9u;
    uint64_t totalEmitted_ = 0;
    uint64_t totalRetired_ = 0;
};

} // namespace dash::vfx
