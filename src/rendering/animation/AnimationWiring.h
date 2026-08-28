#pragma once

// Header-only on purpose: vulkan_experimental lists its sources explicitly in
// the root CMakeLists, so a new .cpp here would silently never be compiled.

#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rendering/animation/AnimationClip.h"
#include "rendering/animation/AnimationFile.h"
#include "rendering/animation/AnimationPlayer.h"
#include "rendering/animation/Skeleton.h"
#include "rendering/vulkan/RenderTypes.h"

namespace dash::anim {

// Skeleton plus every clip shipped with one model, shared by all instances of it.
struct AnimationSet {
    std::shared_ptr<const Skeleton> skeleton;
    std::vector<std::shared_ptr<const AnimationClip>> clips;

    bool valid() const { return skeleton != nullptr && !skeleton->empty(); }
};

// One AnimationSet per model path: a hundred wolves read the two files once.
class AnimationSetCache {
public:
    // `meshPath` is the .dashmesh; its .dashskel/.dashanim siblings are derived
    // by swapping the extension. Misses are cached too, so a model without a
    // skeleton is not probed again for every instance that references it.
    const AnimationSet& load(const std::string& meshPath)
    {
        auto it = sets_.find(meshPath);
        if (it != sets_.end()) return it->second;

        std::filesystem::path skelPath(meshPath);
        skelPath.replace_extension(".dashskel");
        std::filesystem::path animPath(meshPath);
        animPath.replace_extension(".dashanim");

        AnimationSet set;
        std::string error;

        auto skeleton = std::make_shared<Skeleton>();
        if (readSkeleton(skelPath.string(), *skeleton, error) && !skeleton->empty()) {
            set.skeleton = skeleton;

            std::vector<AnimationClip> clips;
            if (readAnimationClips(animPath.string(), clips, error)) {
                set.clips.reserve(clips.size());
                for (AnimationClip& clip : clips) {
                    set.clips.push_back(std::make_shared<const AnimationClip>(std::move(clip)));
                }
            }
            std::fprintf(stdout, "[Anim] %s: %u bones, %u clips\n",
                         skelPath.filename().string().c_str(),
                         static_cast<uint32_t>(set.skeleton->boneCount()),
                         static_cast<uint32_t>(set.clips.size()));
        }

        return sets_.emplace(meshPath, std::move(set)).first->second;
    }

    size_t size() const { return sets_.size(); }
    void clear() { sets_.clear(); }

private:
    std::unordered_map<std::string, AnimationSet> sets_;
};

} // namespace dash::anim

namespace dash::vkexp {

// Maps RenderComponent::mesh to a file on disk. Injected so the wiring can be
// exercised without the renderer's asset search paths (and without a device).
using MeshPathResolver = std::function<std::string(const std::string& meshId)>;

// One player per instance carrying an AnimationComponent whose model actually
// has a skeleton, keyed by index into `instances`.
inline std::unordered_map<size_t, dash::anim::AnimationPlayer> buildAnimators(
    const std::vector<RenderInstance>& instances,
    dash::anim::AnimationSetCache& cache,
    const MeshPathResolver& resolveMeshPath)
{
    std::unordered_map<size_t, dash::anim::AnimationPlayer> animators;

    for (size_t i = 0; i < instances.size(); ++i) {
        const RenderInstance& inst = instances[i];
        if (!inst.hasAnimation) continue;

        const std::string meshPath = resolveMeshPath ? resolveMeshPath(inst.meshId)
                                                     : inst.meshId;
        if (meshPath.empty()) continue;

        const dash::anim::AnimationSet& set = cache.load(meshPath);
        if (!set.valid()) continue;

        dash::anim::AnimationPlayer player;
        player.setSkeleton(set.skeleton);
        for (const auto& clip : set.clips) player.addClip(clip);
        player.syncWithComponent(inst.animation);

        animators.emplace(i, std::move(player));
    }

    return animators;
}

} // namespace dash::vkexp
