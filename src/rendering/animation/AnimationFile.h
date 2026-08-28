#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// .dashskel — bone hierarchy that goes with a .dashmesh
//
//   char   magic[4]     "DSKL"
//   uint32 version      1
//   uint32 boneCount
//   float  globalInverse[16]        inverse of the model root node transform
//   per bone:
//     uint16 nameLen; char name[nameLen]
//     int32  parentIndex            -1 for a root bone
//     float  offsetMatrix[16]       inverse bind pose, column-major
//     float  localBind[16]          node transform relative to the parent
//
// .dashanim — every clip found in the source model, in one file
//
//   char   magic[4]     "DANM"
//   uint32 version      1
//   uint32 clipCount
//   per clip:
//     uint16 nameLen; char name[nameLen]
//     float  durationTicks
//     float  ticksPerSecond
//     uint32 channelCount
//     per channel:
//       uint16 nameLen; char boneName[nameLen]
//       uint32 posCount;   per key: float time, float value[3]
//       uint32 rotCount;   per key: float time, float value[4]  (x, y, z, w)
//       uint32 scaleCount; per key: float time, float value[3]
//
// Binary rather than JSON: keyframe data is dense float arrays (a 60-bone clip
// is tens of thousands of floats) where JSON would cost size, parse time and
// round-trip precision, and it matches the .dashmesh sibling already on disk.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "rendering/animation/AnimationClip.h"
#include "rendering/animation/Skeleton.h"

namespace dash::anim {

inline constexpr uint32_t kDashSkelVersion = 1;
inline constexpr uint32_t kDashAnimVersion = 1;

namespace detail {

inline constexpr char kSkelMagic[4] = {'D', 'S', 'K', 'L'};
inline constexpr char kAnimMagic[4] = {'D', 'A', 'N', 'M'};

template <typename T>
inline void writeValue(std::ostream& out, const T& value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
inline bool readValue(std::istream& in, T& value)
{
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return in.good();
}

inline void writeString(std::ostream& out, const std::string& s)
{
    const uint16_t len = static_cast<uint16_t>(s.size() > 0xFFFFu ? 0xFFFFu : s.size());
    writeValue(out, len);
    if (len > 0) out.write(s.data(), len);
}

inline bool readString(std::istream& in, std::string& out)
{
    uint16_t len = 0;
    if (!readValue(in, len)) return false;
    out.resize(len);
    if (len > 0) {
        in.read(&out[0], len);
        if (!in.good()) return false;
    }
    return true;
}

inline void writeMat4(std::ostream& out, const Mat4& m)
{
    out.write(reinterpret_cast<const char*>(m.m), sizeof(float) * 16);
}

inline bool readMat4(std::istream& in, Mat4& m)
{
    in.read(reinterpret_cast<char*>(m.m), sizeof(float) * 16);
    return in.good();
}

inline bool checkMagic(std::istream& in, const char (&expected)[4])
{
    char magic[4] = {};
    in.read(magic, 4);
    return in.good() && std::memcmp(magic, expected, 4) == 0;
}

} // namespace detail

inline bool writeSkeleton(const std::string& path, const Skeleton& skeleton, std::string& outError)
{
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        outError = "cannot open for writing: " + path;
        return false;
    }

    out.write(detail::kSkelMagic, 4);
    detail::writeValue(out, kDashSkelVersion);
    detail::writeValue(out, static_cast<uint32_t>(skeleton.boneCount()));
    detail::writeMat4(out, skeleton.globalInverseTransform());

    for (const Bone& bone : skeleton.bones()) {
        detail::writeString(out, bone.name);
        detail::writeValue(out, static_cast<int32_t>(bone.parent));
        detail::writeMat4(out, bone.offsetMatrix);
        detail::writeMat4(out, bone.localBind);
    }

    if (!out.good()) {
        outError = "write failed: " + path;
        return false;
    }
    return true;
}

inline bool readSkeleton(const std::string& path, Skeleton& outSkeleton, std::string& outError)
{
    outSkeleton.clear();

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        outError = "cannot open for reading: " + path;
        return false;
    }
    if (!detail::checkMagic(in, detail::kSkelMagic)) {
        outError = "bad magic, not a .dashskel: " + path;
        return false;
    }

    uint32_t version = 0;
    uint32_t boneCount = 0;
    if (!detail::readValue(in, version) || !detail::readValue(in, boneCount)) {
        outError = "truncated header: " + path;
        return false;
    }
    if (version != kDashSkelVersion) {
        outError = "unsupported .dashskel version " + std::to_string(version);
        return false;
    }

    Mat4 globalInverse{};
    if (!detail::readMat4(in, globalInverse)) {
        outError = "truncated global inverse: " + path;
        return false;
    }
    outSkeleton.setGlobalInverseTransform(globalInverse);

    for (uint32_t i = 0; i < boneCount; ++i) {
        std::string name;
        int32_t parent = -1;
        Mat4 offset{};
        Mat4 localBind{};
        if (!detail::readString(in, name) || !detail::readValue(in, parent) ||
            !detail::readMat4(in, offset) || !detail::readMat4(in, localBind)) {
            outError = "truncated bone " + std::to_string(i) + " in " + path;
            return false;
        }
        outSkeleton.addBone(name, parent, offset, localBind);
    }

    return true;
}

inline bool writeAnimationClips(const std::string& path,
                                const std::vector<AnimationClip>& clips,
                                std::string& outError)
{
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        outError = "cannot open for writing: " + path;
        return false;
    }

    out.write(detail::kAnimMagic, 4);
    detail::writeValue(out, kDashAnimVersion);
    detail::writeValue(out, static_cast<uint32_t>(clips.size()));

    for (const AnimationClip& clip : clips) {
        detail::writeString(out, clip.name);
        detail::writeValue(out, clip.duration);
        detail::writeValue(out, clip.ticksPerSecond);
        detail::writeValue(out, static_cast<uint32_t>(clip.channels.size()));

        for (const AnimationChannel& channel : clip.channels) {
            detail::writeString(out, channel.boneName);

            detail::writeValue(out, static_cast<uint32_t>(channel.positions.size()));
            for (const VecKey& key : channel.positions) {
                detail::writeValue(out, key.time);
                detail::writeValue(out, key.value);
            }

            detail::writeValue(out, static_cast<uint32_t>(channel.rotations.size()));
            for (const QuatKey& key : channel.rotations) {
                detail::writeValue(out, key.time);
                detail::writeValue(out, key.value);
            }

            detail::writeValue(out, static_cast<uint32_t>(channel.scales.size()));
            for (const VecKey& key : channel.scales) {
                detail::writeValue(out, key.time);
                detail::writeValue(out, key.value);
            }
        }
    }

    if (!out.good()) {
        outError = "write failed: " + path;
        return false;
    }
    return true;
}

inline bool readAnimationClips(const std::string& path,
                               std::vector<AnimationClip>& outClips,
                               std::string& outError)
{
    outClips.clear();

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        outError = "cannot open for reading: " + path;
        return false;
    }
    if (!detail::checkMagic(in, detail::kAnimMagic)) {
        outError = "bad magic, not a .dashanim: " + path;
        return false;
    }

    uint32_t version = 0;
    uint32_t clipCount = 0;
    if (!detail::readValue(in, version) || !detail::readValue(in, clipCount)) {
        outError = "truncated header: " + path;
        return false;
    }
    if (version != kDashAnimVersion) {
        outError = "unsupported .dashanim version " + std::to_string(version);
        return false;
    }

    outClips.resize(clipCount);
    for (uint32_t c = 0; c < clipCount; ++c) {
        AnimationClip& clip = outClips[c];
        uint32_t channelCount = 0;
        if (!detail::readString(in, clip.name) || !detail::readValue(in, clip.duration) ||
            !detail::readValue(in, clip.ticksPerSecond) || !detail::readValue(in, channelCount)) {
            outError = "truncated clip " + std::to_string(c) + " in " + path;
            return false;
        }

        clip.channels.resize(channelCount);
        for (AnimationChannel& channel : clip.channels) {
            if (!detail::readString(in, channel.boneName)) {
                outError = "truncated channel name in " + path;
                return false;
            }

            uint32_t count = 0;
            if (!detail::readValue(in, count)) { outError = "truncated position keys in " + path; return false; }
            channel.positions.resize(count);
            for (VecKey& key : channel.positions) {
                if (!detail::readValue(in, key.time) || !detail::readValue(in, key.value)) {
                    outError = "truncated position key in " + path;
                    return false;
                }
            }

            if (!detail::readValue(in, count)) { outError = "truncated rotation keys in " + path; return false; }
            channel.rotations.resize(count);
            for (QuatKey& key : channel.rotations) {
                if (!detail::readValue(in, key.time) || !detail::readValue(in, key.value)) {
                    outError = "truncated rotation key in " + path;
                    return false;
                }
            }

            if (!detail::readValue(in, count)) { outError = "truncated scale keys in " + path; return false; }
            channel.scales.resize(count);
            for (VecKey& key : channel.scales) {
                if (!detail::readValue(in, key.time) || !detail::readValue(in, key.value)) {
                    outError = "truncated scale key in " + path;
                    return false;
                }
            }
        }
    }

    return true;
}

} // namespace dash::anim
