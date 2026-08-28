#pragma once

// Header-only on purpose: ModelImporter.cpp is compiled standalone into several
// existing test targets, so the skeleton types must not add link dependencies.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "rendering/animation/AnimMath.h"
#include "rendering/mesh/SkinnedVertex.h"

namespace dash::anim {

struct Bone {
    std::string name;
    int         parent = -1;                // index into Skeleton::bones(), -1 = root
    Mat4        offsetMatrix = identity();  // inverse bind pose (mesh space -> bone space)
    Mat4        localBind = identity();     // node transform relative to the parent
};

// Flat, topologically sorted bone hierarchy: a bone's parent always has a lower
// index, so pose evaluation is a single forward pass with no recursion.
class Skeleton {
public:
    static constexpr uint32_t kMaxBones = dash::vkexp::kMaxBonesPerSkeleton;

    int addBone(const std::string& name, int parent,
                const Mat4& offsetMatrix, const Mat4& localBind)
    {
        auto it = nameToIndex_.find(name);
        if (it != nameToIndex_.end()) return it->second;

        const int index = static_cast<int>(bones_.size());
        bones_.push_back(Bone{name, parent, offsetMatrix, localBind});
        nameToIndex_.emplace(name, index);
        return index;
    }

    int findBone(const std::string& name) const
    {
        auto it = nameToIndex_.find(name);
        return it == nameToIndex_.end() ? -1 : it->second;
    }

    // The inverse bind pose only exists on aiBone, so it is filled in a second
    // pass after the hierarchy has been walked.
    void setBoneOffset(int index, const Mat4& offset)
    {
        if (index >= 0 && index < static_cast<int>(bones_.size())) {
            bones_[static_cast<size_t>(index)].offsetMatrix = offset;
        }
    }

    const std::vector<Bone>& bones() const { return bones_; }
    size_t boneCount() const { return bones_.size(); }
    bool empty() const { return bones_.empty(); }
    bool exceedsGpuLimit() const { return bones_.size() > kMaxBones; }

    const Mat4& globalInverseTransform() const { return globalInverse_; }
    void setGlobalInverseTransform(const Mat4& m) { globalInverse_ = m; }

    bool isTopologicallySorted() const
    {
        for (size_t i = 0; i < bones_.size(); ++i) {
            if (bones_[i].parent >= static_cast<int>(i)) return false;
        }
        return true;
    }

    // Bone matrices for the bind pose; every entry is the identity when the
    // offset matrices are true inverse bind poses. Used when nothing is playing.
    void bindPoseMatrices(std::vector<Mat4>& out) const
    {
        out.assign(bones_.size(), identity());
        std::vector<Mat4> globals(bones_.size(), identity());

        for (size_t i = 0; i < bones_.size(); ++i) {
            const Bone& bone = bones_[i];
            globals[i] = bone.parent >= 0
                ? multiply(globals[static_cast<size_t>(bone.parent)], bone.localBind)
                : bone.localBind;
            out[i] = multiply(multiply(globalInverse_, globals[i]), bone.offsetMatrix);
        }
    }

    void clear()
    {
        bones_.clear();
        nameToIndex_.clear();
        globalInverse_ = identity();
    }

private:
    std::vector<Bone> bones_;
    std::unordered_map<std::string, int> nameToIndex_;
    Mat4 globalInverse_ = identity();
};

} // namespace dash::anim
