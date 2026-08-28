#include "rendering/animation/AnimationPlayer.h"

#include <algorithm>

#include "core/components/Components.h"
#include "rendering/animation/AnimationFile.h"

namespace dash::anim {

void AnimationPlayer::Track::advance(float deltaSeconds, float speed, bool loop)
{
    if (!clip) return;
    const float tps = clip->ticksPerSecond > 0.0f ? clip->ticksPerSecond : 25.0f;
    const float step = deltaSeconds * speed * tps;
    elapsedTicks += step;
    timeTicks = clip->normalizeTime(timeTicks + step, loop);
}

void AnimationPlayer::setSkeleton(std::shared_ptr<const Skeleton> skeleton)
{
    skeleton_ = std::move(skeleton);
    if (skeleton_) {
        skeleton_->bindPoseMatrices(boneMatrices_);
    } else {
        boneMatrices_.clear();
    }
}

void AnimationPlayer::addClip(std::shared_ptr<const AnimationClip> clip)
{
    if (!clip || clip->name.empty()) return;
    clips_[clip->name] = std::move(clip);
}

const AnimationClip* AnimationPlayer::findClip(const std::string& name) const
{
    auto it = clips_.find(name);
    return it == clips_.end() ? nullptr : it->second.get();
}

bool AnimationPlayer::play(const std::string& clipName, float blendSeconds)
{
    if (clipName == current_.name && current_.clip) return true;

    const AnimationClip* clip = findClip(clipName);
    if (!clip) return false;

    if (current_.clip && blendSeconds > 0.0f) {
        previous_ = current_;
        blendDuration_ = blendSeconds;
        blendElapsed_ = 0.0f;
    } else {
        previous_.reset();
        blendDuration_ = 0.0f;
        blendElapsed_ = 0.0f;
    }

    current_.clip = clip;
    current_.name = clipName;
    current_.timeTicks = 0.0f;
    current_.elapsedTicks = 0.0f;
    evaluate();
    return true;
}

void AnimationPlayer::stop()
{
    current_.reset();
    previous_.reset();
    blendDuration_ = 0.0f;
    blendElapsed_ = 0.0f;
    if (skeleton_) skeleton_->bindPoseMatrices(boneMatrices_);
}

void AnimationPlayer::setStateMachine(std::shared_ptr<const AnimationStateMachine> machine)
{
    if (machine) {
        stateMachine_.setMachine(std::move(machine));
    } else {
        stateMachine_.clearMachine();
    }
}

void AnimationPlayer::syncWithComponent(const AnimationComponent& component)
{
    paused_ = !component.playing;

    // While a controller is installed it owns clip/speed/loop, so re-reading the
    // component every frame cannot undo the transition it just took.
    if (stateMachine_.active()) return;

    speed_ = component.speed;
    loop_ = component.loop;

    if (component.clip.empty()) {
        if (current_.clip) stop();
        return;
    }
    play(component.clip, component.blendSeconds);
}

float AnimationPlayer::currentTimeSeconds() const
{
    if (!current_.clip) return 0.0f;
    const float tps = current_.clip->ticksPerSecond > 0.0f ? current_.clip->ticksPerSecond : 25.0f;
    return current_.timeTicks / tps;
}

float AnimationPlayer::normalizedClipTime() const
{
    if (!current_.clip || current_.clip->duration <= 0.0f) return 0.0f;
    return current_.elapsedTicks / current_.clip->duration;
}

float AnimationPlayer::blendWeight() const
{
    if (blendDuration_ <= 0.0f) return 1.0f;
    return std::clamp(blendElapsed_ / blendDuration_, 0.0f, 1.0f);
}

void AnimationPlayer::tickStateMachine()
{
    if (!stateMachine_.active()) return;

    const StateMachineRuntime::StepResult result = stateMachine_.step(normalizedClipTime());
    if (!result.changed()) return;

    speed_ = result.state->speed;
    loop_ = result.state->loop;
    play(result.state->clip, result.blendSeconds);
}

void AnimationPlayer::update(float deltaSeconds)
{
    // Runs before the skeleton guard so the graph can be driven (and tested)
    // without a rig, and before advancing so a fresh state starts at t = 0.
    if (!paused_) tickStateMachine();

    if (!skeleton_ || skeleton_->empty()) return;

    if (!paused_ && current_.clip) {
        current_.advance(deltaSeconds, speed_, loop_);
        if (previous_.clip) previous_.advance(deltaSeconds, speed_, true);

        if (blendDuration_ > 0.0f) {
            blendElapsed_ += deltaSeconds;
            if (blendElapsed_ >= blendDuration_) {
                blendDuration_ = 0.0f;
                blendElapsed_ = 0.0f;
                previous_.reset();
            }
        }
    }

    evaluate();
}

void AnimationPlayer::evaluate()
{
    if (!skeleton_ || skeleton_->empty()) return;

    if (!current_.clip) {
        skeleton_->bindPoseMatrices(boneMatrices_);
        return;
    }

    current_.clip->samplePose(*skeleton_, current_.timeTicks, poseB_);

    if (previous_.clip && blendDuration_ > 0.0f) {
        previous_.clip->samplePose(*skeleton_, previous_.timeTicks, poseA_);
        blendPoses(poseA_, poseB_, blendWeight(), blended_);
        poseToBoneMatrices(*skeleton_, blended_, boneMatrices_);
        return;
    }

    poseToBoneMatrices(*skeleton_, poseB_, boneMatrices_);
}

bool loadAnimationSet(const std::string& skeletonPath,
                      const std::string& clipsPath,
                      AnimationPlayer& outPlayer,
                      std::string& outError)
{
    auto skeleton = std::make_shared<Skeleton>();
    if (!readSkeleton(skeletonPath, *skeleton, outError)) return false;

    std::vector<AnimationClip> clips;
    if (!readAnimationClips(clipsPath, clips, outError)) return false;

    outPlayer.setSkeleton(skeleton);
    for (AnimationClip& clip : clips) {
        outPlayer.addClip(std::make_shared<const AnimationClip>(std::move(clip)));
    }
    return true;
}

} // namespace dash::anim
