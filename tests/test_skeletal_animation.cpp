// test_skeletal_animation.cpp — Skeleton, AnimationClip sampling, crossfade
//
// Covers: keyframe interpolation at the clip boundaries and in between,
// loop/clamp time handling, skeleton hierarchy evaluation, .dashskel/.dashanim
// round-trip and AnimationPlayer crossfade blending.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/components/Components.h"
#include "rendering/animation/AnimationFile.h"
#include "rendering/animation/AnimationPlayer.h"

namespace fs = std::filesystem;
using namespace dash::anim;

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn) do { \
    ++tests_run; \
    std::printf("  [%d] %s ... ", tests_run, #fn); \
    fn(); \
    ++tests_passed; \
    std::printf("PASS\n"); \
} while(0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "FAIL at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        std::abort(); \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::fprintf(stderr, "FAIL at %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
        std::abort(); \
    } \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
    const double _d = static_cast<double>(a) - static_cast<double>(b); \
    if (_d > (eps) || _d < -(eps)) { \
        std::fprintf(stderr, "FAIL at %s:%d: %s (%f) != %s (%f)\n", \
                     __FILE__, __LINE__, #a, static_cast<double>(a), \
                     #b, static_cast<double>(b)); \
        std::abort(); \
    } \
} while(0)

static fs::path tempDir()
{
    static const fs::path dir = fs::temp_directory_path() / "dash_test_skeletal";
    fs::create_directories(dir);
    return dir;
}

// Two-bone chain: "root" at the origin, "child" one unit up.
static std::shared_ptr<Skeleton> makeChainSkeleton()
{
    auto skeleton = std::make_shared<Skeleton>();
    Mat4 childBind = identity();
    childBind.m[13] = 1.0f;
    skeleton->addBone("root", -1, identity(), identity());
    skeleton->addBone("child", 0, identity(), childBind);
    return skeleton;
}

// Moves "child" along X from `from` to `to` over 10 ticks at 10 ticks/second.
static AnimationClip makeSlideClip(const std::string& name, float from, float to)
{
    AnimationChannel channel;
    channel.boneName = "child";
    channel.positions = {
        VecKey{0.0f,  {from, 0.0f, 0.0f}},
        VecKey{10.0f, {to,   0.0f, 0.0f}},
    };
    channel.rotations = {QuatKey{0.0f, Quat{}}};
    channel.scales = {VecKey{0.0f, {1.0f, 1.0f, 1.0f}}};

    AnimationClip clip;
    clip.name = name;
    clip.duration = 10.0f;
    clip.ticksPerSecond = 10.0f;
    clip.channels.push_back(channel);
    return clip;
}

// ─── Test: sampling hits the first key at t=0 and the last at t=duration ─────
static void test_channel_sampling_endpoints_and_midpoint()
{
    AnimationChannel channel;
    channel.boneName = "child";
    channel.positions = {
        VecKey{0.0f,  {0.0f, 0.0f, 0.0f}},
        VecKey{4.0f,  {8.0f, 0.0f, 0.0f}},
        VecKey{10.0f, {8.0f, 6.0f, 0.0f}},
    };

    const Vec3 start = channel.samplePosition(0.0f);
    ASSERT_NEAR(start.x, 0.0f, 1e-5f);
    ASSERT_NEAR(start.y, 0.0f, 1e-5f);

    const Vec3 end = channel.samplePosition(10.0f);
    ASSERT_NEAR(end.x, 8.0f, 1e-5f);
    ASSERT_NEAR(end.y, 6.0f, 1e-5f);

    // Halfway through the first segment.
    const Vec3 mid = channel.samplePosition(2.0f);
    ASSERT_NEAR(mid.x, 4.0f, 1e-5f);
    ASSERT_NEAR(mid.y, 0.0f, 1e-5f);

    // Halfway through the second segment.
    const Vec3 mid2 = channel.samplePosition(7.0f);
    ASSERT_NEAR(mid2.x, 8.0f, 1e-5f);
    ASSERT_NEAR(mid2.y, 3.0f, 1e-5f);

    // Past the end clamps to the last key instead of extrapolating.
    const Vec3 beyond = channel.samplePosition(99.0f);
    ASSERT_NEAR(beyond.y, 6.0f, 1e-5f);
}

// ─── Test: rotation keys are slerped, not lerped ─────────────────────────────
static void test_rotation_sampling_is_normalized()
{
    const float half = 3.14159265f * 0.5f;  // 90 degrees about Y
    AnimationChannel channel;
    channel.boneName = "child";
    channel.rotations = {
        QuatKey{0.0f,  Quat{0.0f, 0.0f, 0.0f, 1.0f}},
        QuatKey{10.0f, Quat{0.0f, std::sin(half * 0.5f), 0.0f, std::cos(half * 0.5f)}},
    };

    const Quat begin = channel.sampleRotation(0.0f);
    ASSERT_NEAR(begin.w, 1.0f, 1e-5f);

    const Quat mid = channel.sampleRotation(5.0f);
    const float length = std::sqrt(mid.x * mid.x + mid.y * mid.y + mid.z * mid.z + mid.w * mid.w);
    ASSERT_NEAR(length, 1.0f, 1e-5f);
    // Halfway to 90 degrees is 45 degrees: y = sin(22.5deg).
    ASSERT_NEAR(mid.y, std::sin(half * 0.25f), 1e-4f);

    const Quat end = channel.sampleRotation(10.0f);
    ASSERT_NEAR(end.y, std::sin(half * 0.5f), 1e-5f);
}

// ─── Test: loop wraps, non-loop clamps ───────────────────────────────────────
static void test_time_normalization()
{
    const AnimationClip clip = makeSlideClip("walk", 0.0f, 10.0f);

    ASSERT_NEAR(clip.normalizeTime(12.0f, true), 2.0f, 1e-5f);
    ASSERT_NEAR(clip.normalizeTime(12.0f, false), 10.0f, 1e-5f);
    ASSERT_NEAR(clip.normalizeTime(-1.0f, true), 9.0f, 1e-5f);
    ASSERT_NEAR(clip.normalizeTime(-1.0f, false), 0.0f, 1e-5f);
    ASSERT_NEAR(clip.durationSeconds(), 1.0f, 1e-5f);
}

// ─── Test: skeleton hierarchy and bind pose ──────────────────────────────────
static void test_skeleton_hierarchy()
{
    auto skeleton = makeChainSkeleton();

    ASSERT_EQ(skeleton->boneCount(), 2u);
    ASSERT_EQ(skeleton->findBone("root"), 0);
    ASSERT_EQ(skeleton->findBone("child"), 1);
    ASSERT_EQ(skeleton->findBone("missing"), -1);
    ASSERT_TRUE(skeleton->isTopologicallySorted());
    ASSERT_TRUE(!skeleton->exceedsGpuLimit());

    // Bind pose with identity offsets leaves the child's local translation.
    std::vector<Mat4> matrices;
    skeleton->bindPoseMatrices(matrices);
    ASSERT_EQ(matrices.size(), 2u);
    ASSERT_NEAR(matrices[1].m[13], 1.0f, 1e-5f);

    // Adding the same bone twice returns the existing index.
    ASSERT_EQ(skeleton->addBone("child", 0, identity(), identity()), 1);
    ASSERT_EQ(skeleton->boneCount(), 2u);
}

// ─── Test: pose evaluation walks parents into children ───────────────────────
static void test_pose_to_bone_matrices_accumulates_parents()
{
    auto skeleton = makeChainSkeleton();

    std::vector<BonePose> pose(2);
    pose[0].translation = {5.0f, 0.0f, 0.0f};
    pose[0].animated = true;
    pose[1].translation = {0.0f, 2.0f, 0.0f};
    pose[1].animated = true;

    std::vector<Mat4> matrices;
    poseToBoneMatrices(*skeleton, pose, matrices);

    ASSERT_NEAR(matrices[0].m[12], 5.0f, 1e-5f);
    ASSERT_NEAR(matrices[1].m[12], 5.0f, 1e-5f);   // inherited from the parent
    ASSERT_NEAR(matrices[1].m[13], 2.0f, 1e-5f);
}

// ─── Test: .dashskel / .dashanim round-trip ──────────────────────────────────
static void test_skeleton_and_clip_serialization()
{
    auto skeleton = makeChainSkeleton();
    Mat4 globalInverse = identity();
    globalInverse.m[14] = -3.0f;
    skeleton->setGlobalInverseTransform(globalInverse);
    skeleton->setBoneOffset(1, composeTRS({0.0f, -1.0f, 0.0f}, Quat{}, {1.0f, 1.0f, 1.0f}));

    const std::string skelPath = (tempDir() / "chain.dashskel").string();
    const std::string animPath = (tempDir() / "chain.dashanim").string();

    std::vector<AnimationClip> clips = {
        makeSlideClip("idle", 0.0f, 0.0f),
        makeSlideClip("run", 0.0f, 4.0f),
    };

    std::string error;
    ASSERT_TRUE(writeSkeleton(skelPath, *skeleton, error));
    ASSERT_TRUE(writeAnimationClips(animPath, clips, error));

    Skeleton loadedSkeleton;
    ASSERT_TRUE(readSkeleton(skelPath, loadedSkeleton, error));
    ASSERT_EQ(loadedSkeleton.boneCount(), 2u);
    ASSERT_EQ(loadedSkeleton.bones()[1].name, std::string("child"));
    ASSERT_EQ(loadedSkeleton.bones()[1].parent, 0);
    ASSERT_NEAR(loadedSkeleton.globalInverseTransform().m[14], -3.0f, 1e-5f);
    ASSERT_NEAR(loadedSkeleton.bones()[1].offsetMatrix.m[13], -1.0f, 1e-5f);
    ASSERT_NEAR(loadedSkeleton.bones()[1].localBind.m[13], 1.0f, 1e-5f);

    std::vector<AnimationClip> loadedClips;
    ASSERT_TRUE(readAnimationClips(animPath, loadedClips, error));
    ASSERT_EQ(loadedClips.size(), 2u);
    ASSERT_EQ(loadedClips[1].name, std::string("run"));
    ASSERT_NEAR(loadedClips[1].duration, 10.0f, 1e-5f);
    ASSERT_NEAR(loadedClips[1].ticksPerSecond, 10.0f, 1e-5f);
    ASSERT_EQ(loadedClips[1].channels.size(), 1u);
    ASSERT_EQ(loadedClips[1].channels[0].positions.size(), 2u);
    ASSERT_NEAR(loadedClips[1].channels[0].positions[1].value.x, 4.0f, 1e-5f);

    // And the loader helper wires both files into a player in one call.
    AnimationPlayer player;
    ASSERT_TRUE(loadAnimationSet(skelPath, animPath, player, error));
    ASSERT_EQ(player.clipCount(), 2u);
    ASSERT_TRUE(player.findClip("idle") != nullptr);

    fs::remove(skelPath);
    fs::remove(animPath);
}

// ─── Test: player advances time and honours speed ────────────────────────────
static void test_player_advances_and_loops()
{
    AnimationPlayer player;
    player.setSkeleton(makeChainSkeleton());
    player.addClip(std::make_shared<const AnimationClip>(makeSlideClip("walk", 0.0f, 10.0f)));

    ASSERT_TRUE(!player.play("missing"));
    ASSERT_TRUE(player.play("walk"));
    ASSERT_EQ(player.currentClipName(), std::string("walk"));

    // 0.5s at 10 ticks/s = tick 5 = halfway along the slide.
    player.update(0.5f);
    ASSERT_NEAR(player.boneMatrices()[1].m[12], 5.0f, 1e-4f);

    // Looping wraps back near the start rather than clamping at the end.
    player.update(0.6f);
    ASSERT_NEAR(player.currentTimeSeconds(), 0.1f, 1e-4f);

    // Double speed covers twice the ticks.
    player.play("walk");
    player.stop();
    player.play("walk");
    player.setSpeed(2.0f);
    player.update(0.25f);
    ASSERT_NEAR(player.boneMatrices()[1].m[12], 5.0f, 1e-4f);

    // Paused playback freezes the pose.
    player.setPaused(true);
    const float frozen = player.boneMatrices()[1].m[12];
    player.update(1.0f);
    ASSERT_NEAR(player.boneMatrices()[1].m[12], frozen, 1e-5f);
}

// ─── Test: halfway through a crossfade the pose is between both clips ────────
static void test_crossfade_lands_between_clips()
{
    AnimationPlayer player;
    player.setSkeleton(makeChainSkeleton());
    // "low" parks the child at x=0, "high" parks it at x=10, both constant so
    // the blended value depends only on the crossfade weight.
    player.addClip(std::make_shared<const AnimationClip>(makeSlideClip("low", 0.0f, 0.0f)));
    player.addClip(std::make_shared<const AnimationClip>(makeSlideClip("high", 10.0f, 10.0f)));

    ASSERT_TRUE(player.play("low"));
    player.update(0.0f);
    ASSERT_NEAR(player.boneMatrices()[1].m[12], 0.0f, 1e-4f);

    ASSERT_TRUE(player.play("high", 1.0f));
    ASSERT_TRUE(player.isBlending());

    // Quarter of the way through the blend.
    player.update(0.25f);
    ASSERT_NEAR(player.blendWeight(), 0.25f, 1e-5f);
    ASSERT_NEAR(player.boneMatrices()[1].m[12], 2.5f, 1e-4f);

    // Halfway: strictly between both clips.
    player.update(0.25f);
    ASSERT_NEAR(player.blendWeight(), 0.5f, 1e-5f);
    const float mid = player.boneMatrices()[1].m[12];
    ASSERT_TRUE(mid > 0.0f && mid < 10.0f);
    ASSERT_NEAR(mid, 5.0f, 1e-4f);

    // Once the blend elapses only the incoming clip remains.
    player.update(0.6f);
    ASSERT_TRUE(!player.isBlending());
    ASSERT_NEAR(player.boneMatrices()[1].m[12], 10.0f, 1e-4f);
}

// ─── Test: blendPoses on partially animated bones ────────────────────────────
static void test_blend_prefers_the_animated_side()
{
    std::vector<BonePose> a(2);
    std::vector<BonePose> b(2);
    a[0].translation = {4.0f, 0.0f, 0.0f};
    a[0].animated = true;
    b[1].translation = {0.0f, 8.0f, 0.0f};
    b[1].animated = true;

    std::vector<BonePose> out;
    blendPoses(a, b, 0.5f, out);

    ASSERT_EQ(out.size(), 2u);
    ASSERT_NEAR(out[0].translation.x, 4.0f, 1e-5f);   // only `a` animates bone 0
    ASSERT_NEAR(out[1].translation.y, 8.0f, 1e-5f);   // only `b` animates bone 1
}

// ─── Test: AnimationComponent drives the player ──────────────────────────────
static void test_sync_with_animation_component()
{
    AnimationPlayer player;
    player.setSkeleton(makeChainSkeleton());
    player.addClip(std::make_shared<const AnimationClip>(makeSlideClip("idle", 0.0f, 0.0f)));
    player.addClip(std::make_shared<const AnimationClip>(makeSlideClip("run", 10.0f, 10.0f)));

    AnimationComponent component;
    component.clip = "idle";
    component.speed = 2.0f;
    component.loop = false;
    component.playing = true;
    component.blendSeconds = 0.5f;

    player.syncWithComponent(component);
    ASSERT_EQ(player.currentClipName(), std::string("idle"));
    ASSERT_NEAR(player.speed(), 2.0f, 1e-5f);
    ASSERT_TRUE(!player.loop());
    ASSERT_TRUE(!player.paused());

    // Switching clip through the component starts a crossfade of blendSeconds.
    component.clip = "run";
    player.syncWithComponent(component);
    ASSERT_EQ(player.currentClipName(), std::string("run"));
    ASSERT_TRUE(player.isBlending());
    player.update(0.25f);
    ASSERT_NEAR(player.blendWeight(), 0.5f, 1e-5f);
    ASSERT_NEAR(player.boneMatrices()[1].m[12], 5.0f, 1e-4f);

    // Re-syncing the same clip must not restart it.
    const float before = player.boneMatrices()[1].m[12];
    player.syncWithComponent(component);
    ASSERT_NEAR(player.boneMatrices()[1].m[12], before, 1e-5f);

    // An empty clip name falls back to the bind pose.
    component.clip.clear();
    player.syncWithComponent(component);
    ASSERT_TRUE(player.currentClipName().empty());
    ASSERT_NEAR(player.boneMatrices()[1].m[12], 0.0f, 1e-5f);
}

int main()
{
    std::printf("Running skeletal animation tests\n");

    RUN_TEST(test_channel_sampling_endpoints_and_midpoint);
    RUN_TEST(test_rotation_sampling_is_normalized);
    RUN_TEST(test_time_normalization);
    RUN_TEST(test_skeleton_hierarchy);
    RUN_TEST(test_pose_to_bone_matrices_accumulates_parents);
    RUN_TEST(test_skeleton_and_clip_serialization);
    RUN_TEST(test_player_advances_and_loops);
    RUN_TEST(test_crossfade_lands_between_clips);
    RUN_TEST(test_blend_prefers_the_animated_side);
    RUN_TEST(test_sync_with_animation_component);

    fs::remove_all(tempDir());

    std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
