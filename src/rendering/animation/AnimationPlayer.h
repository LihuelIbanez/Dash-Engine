#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "rendering/animation/AnimationClip.h"
#include "rendering/animation/AnimationStateMachine.h"
#include "rendering/animation/Skeleton.h"

struct AnimationComponent;

namespace dash::anim {

// Drives one skinned instance: advances clip time, samples keyframes and
// produces the final bone matrix array. Supports a single crossfade between an
// outgoing and an incoming clip, which is what AnimationComponent exposes.
class AnimationPlayer {
public:
    void setSkeleton(std::shared_ptr<const Skeleton> skeleton);
    const Skeleton* skeleton() const { return skeleton_.get(); }

    void addClip(std::shared_ptr<const AnimationClip> clip);
    const AnimationClip* findClip(const std::string& name) const;
    size_t clipCount() const { return clips_.size(); }

    // Starting the clip that is already playing is a no-op, so calling this
    // every frame from component state is safe.
    bool play(const std::string& clipName, float blendSeconds = 0.0f);
    void stop();

    void setSpeed(float speed) { speed_ = speed; }
    float speed() const { return speed_; }
    void setLoop(bool loop) { loop_ = loop; }
    bool loop() const { return loop_; }
    void setPaused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }

    // Installing a machine hands clip/speed/loop over to it; passing nullptr
    // gives them back to AnimationComponent.
    void setStateMachine(std::shared_ptr<const AnimationStateMachine> machine);
    StateMachineRuntime& stateMachine() { return stateMachine_; }
    const StateMachineRuntime& stateMachine() const { return stateMachine_; }
    bool stateMachineActive() const { return stateMachine_.active(); }

    // Mirrors clip/speed/loop/playing/blendSeconds from the ECS component.
    void syncWithComponent(const AnimationComponent& component);

    void update(float deltaSeconds);

    const std::vector<Mat4>& boneMatrices() const { return boneMatrices_; }
    const std::string& currentClipName() const { return current_.name; }
    float currentTimeSeconds() const;
    // Playthroughs of the current clip since it started; 1.0 = one full pass.
    // Unlike currentTimeSeconds() it does not wrap, so looping states can gate
    // on exit time too.
    float normalizedClipTime() const;
    bool isBlending() const { return blendDuration_ > 0.0f && blendElapsed_ < blendDuration_; }
    float blendWeight() const;

private:
    struct Track {
        const AnimationClip* clip = nullptr;
        std::string name;
        float timeTicks = 0.0f;
        float elapsedTicks = 0.0f;   // monotonic, ignores looping

        void advance(float deltaSeconds, float speed, bool loop);
        void reset() { clip = nullptr; name.clear(); timeTicks = 0.0f; elapsedTicks = 0.0f; }
    };

    void evaluate();
    void tickStateMachine();

    std::shared_ptr<const Skeleton> skeleton_;
    std::unordered_map<std::string, std::shared_ptr<const AnimationClip>> clips_;
    StateMachineRuntime stateMachine_;

    Track current_;
    Track previous_;

    float speed_ = 1.0f;
    bool  loop_ = true;
    bool  paused_ = false;

    float blendDuration_ = 0.0f;
    float blendElapsed_ = 0.0f;

    std::vector<BonePose> poseA_;
    std::vector<BonePose> poseB_;
    std::vector<BonePose> blended_;
    std::vector<Mat4> boneMatrices_;
};

// Convenience wiring: reads the .dashskel/.dashanim pair written next to a
// .dashmesh and installs skeleton and clips into `outPlayer`.
bool loadAnimationSet(const std::string& skeletonPath,
                      const std::string& clipsPath,
                      AnimationPlayer& outPlayer,
                      std::string& outError);

} // namespace dash::anim
