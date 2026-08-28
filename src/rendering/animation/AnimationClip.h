#pragma once

// Header-only for the same reason as Skeleton.h: the importer must be able to
// build clips without pulling in extra translation units.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "rendering/animation/AnimMath.h"
#include "rendering/animation/Skeleton.h"

namespace dash::anim {

struct VecKey {
    float time = 0.0f;   // in ticks
    Vec3  value{0.0f, 0.0f, 0.0f};
};

struct QuatKey {
    float time = 0.0f;   // in ticks
    Quat  value{};
};

// Local-space TRS for one bone at one instant. `animated` is false when the clip
// has no channel for that bone, in which case the skeleton bind pose wins.
struct BonePose {
    Vec3 translation{0.0f, 0.0f, 0.0f};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    bool animated = false;
};

namespace detail {

// Index of the last key with time <= t plus the factor towards the next one.
// Keys are sorted by time (Assimp guarantees it, and the writer preserves order).
template <typename KeyT>
inline size_t findKeySegment(const std::vector<KeyT>& keys, float t, float& outAlpha)
{
    outAlpha = 0.0f;
    if (keys.size() < 2) return 0;
    if (t <= keys.front().time) return 0;
    if (t >= keys.back().time) return keys.size() - 1;

    size_t lo = 0;
    size_t hi = keys.size() - 1;
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        if (keys[mid].time <= t) lo = mid; else hi = mid;
    }

    const float span = keys[lo + 1].time - keys[lo].time;
    outAlpha = span > 1e-8f ? (t - keys[lo].time) / span : 0.0f;
    return lo;
}

} // namespace detail

struct AnimationChannel {
    std::string boneName;
    std::vector<VecKey>  positions;
    std::vector<QuatKey> rotations;
    std::vector<VecKey>  scales;

    bool empty() const { return positions.empty() && rotations.empty() && scales.empty(); }

    Vec3 samplePosition(float timeTicks) const
    {
        if (positions.empty()) return {0.0f, 0.0f, 0.0f};
        float alpha = 0.0f;
        const size_t i = detail::findKeySegment(positions, timeTicks, alpha);
        if (i + 1 >= positions.size()) return positions[i].value;
        return lerp(positions[i].value, positions[i + 1].value, alpha);
    }

    Quat sampleRotation(float timeTicks) const
    {
        if (rotations.empty()) return Quat{};
        float alpha = 0.0f;
        const size_t i = detail::findKeySegment(rotations, timeTicks, alpha);
        if (i + 1 >= rotations.size()) return rotations[i].value;
        return slerp(rotations[i].value, rotations[i + 1].value, alpha);
    }

    Vec3 sampleScale(float timeTicks) const
    {
        if (scales.empty()) return {1.0f, 1.0f, 1.0f};
        float alpha = 0.0f;
        const size_t i = detail::findKeySegment(scales, timeTicks, alpha);
        if (i + 1 >= scales.size()) return scales[i].value;
        return lerp(scales[i].value, scales[i + 1].value, alpha);
    }
};

struct AnimationClip {
    std::string name;
    float duration = 0.0f;          // in ticks
    float ticksPerSecond = 25.0f;   // Assimp's fallback when the file omits it
    std::vector<AnimationChannel> channels;

    float durationSeconds() const
    {
        return ticksPerSecond > 0.0f ? duration / ticksPerSecond : duration;
    }

    const AnimationChannel* findChannel(const std::string& boneName) const
    {
        for (const AnimationChannel& channel : channels) {
            if (channel.boneName == boneName) return &channel;
        }
        return nullptr;
    }

    // Wraps into [0, duration) when looping, clamps to [0, duration] otherwise.
    float normalizeTime(float timeTicks, bool loop) const
    {
        if (duration <= 0.0f) return 0.0f;
        if (!loop) return std::clamp(timeTicks, 0.0f, duration);

        float wrapped = std::fmod(timeTicks, duration);
        if (wrapped < 0.0f) wrapped += duration;
        return wrapped;
    }

    void samplePose(const Skeleton& skeleton, float timeTicks, std::vector<BonePose>& out) const
    {
        const std::vector<Bone>& bones = skeleton.bones();
        out.assign(bones.size(), BonePose{});

        for (size_t i = 0; i < bones.size(); ++i) {
            const AnimationChannel* channel = findChannel(bones[i].name);
            if (!channel || channel->empty()) continue;

            out[i].translation = channel->samplePosition(timeTicks);
            out[i].rotation    = channel->sampleRotation(timeTicks);
            out[i].scale       = channel->sampleScale(timeTicks);
            out[i].animated    = true;
        }
    }
};

// Per-bone blend of two local poses; weight 0 = a, 1 = b.
inline void blendPoses(const std::vector<BonePose>& a,
                       const std::vector<BonePose>& b,
                       float weight,
                       std::vector<BonePose>& out)
{
    const size_t count = std::max(a.size(), b.size());
    out.assign(count, BonePose{});
    const float w = std::clamp(weight, 0.0f, 1.0f);

    for (size_t i = 0; i < count; ++i) {
        const BonePose* pa = i < a.size() ? &a[i] : nullptr;
        const BonePose* pb = i < b.size() ? &b[i] : nullptr;
        const bool aAnim = pa && pa->animated;
        const bool bAnim = pb && pb->animated;

        if (aAnim && bAnim) {
            out[i].translation = lerp(pa->translation, pb->translation, w);
            out[i].rotation    = slerp(pa->rotation, pb->rotation, w);
            out[i].scale       = lerp(pa->scale, pb->scale, w);
            out[i].animated    = true;
        } else if (aAnim) {
            out[i] = *pa;
        } else if (bAnim) {
            out[i] = *pb;
        }
    }
}

// Turns local poses into the final skinning matrices uploaded to the shader.
inline void poseToBoneMatrices(const Skeleton& skeleton,
                               const std::vector<BonePose>& pose,
                               std::vector<Mat4>& out)
{
    const std::vector<Bone>& bones = skeleton.bones();
    out.assign(bones.size(), identity());
    std::vector<Mat4> globals(bones.size(), identity());

    for (size_t i = 0; i < bones.size(); ++i) {
        const Bone& bone = bones[i];
        const bool animated = i < pose.size() && pose[i].animated;
        const Mat4 local = animated
            ? composeTRS(pose[i].translation, pose[i].rotation, pose[i].scale)
            : bone.localBind;

        globals[i] = bone.parent >= 0
            ? multiply(globals[static_cast<size_t>(bone.parent)], local)
            : local;

        out[i] = multiply(multiply(skeleton.globalInverseTransform(), globals[i]),
                          bone.offsetMatrix);
    }
}

} // namespace dash::anim
