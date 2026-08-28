// test_animation_wiring.cpp — scene AnimationComponent -> AnimationPlayer.
// Covers the part of the pipeline that was missing: nothing ever populated the
// renderer's animator map, so entities with an AnimationComponent stayed in
// bind pose. Everything here is device-free: buildAnimators() only touches the
// RenderInstance vector, the .dashskel/.dashanim files and AnimationPlayer.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/components/Components.h"
#include "rendering/animation/AnimationFile.h"
#include "rendering/animation/AnimationWiring.h"

using namespace dash::anim;
using dash::vkexp::RenderInstance;
using dash::vkexp::buildAnimators;

namespace fs = std::filesystem;

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
    if (std::fabs((a) - (b)) > (eps)) { \
        std::fprintf(stderr, "FAIL at %s:%d: %s (%f) != %s (%f)\n", \
                     __FILE__, __LINE__, #a, static_cast<double>(a), \
                     #b, static_cast<double>(b)); \
        std::abort(); \
    } \
} while(0)

// ─── Fixture: a two-bone model with one clip that slides the child on X ──────
static const std::string& bakedModelPath()
{
    static std::string meshPath;
    if (!meshPath.empty()) return meshPath;

    const fs::path dir = fs::temp_directory_path() / "dash_test_anim_wiring";
    fs::create_directories(dir);
    meshPath = (dir / "rig.dashmesh").string();

    Skeleton skeleton;
    skeleton.addBone("root", -1, identity(), identity());
    skeleton.addBone("child", 0, identity(), identity());

    AnimationClip clip;
    clip.name = "walk";
    clip.duration = 10.0f;
    clip.ticksPerSecond = 10.0f;
    AnimationChannel channel;
    channel.boneName = "child";
    channel.positions.push_back(VecKey{0.0f, {0.0f, 0.0f, 0.0f}});
    channel.positions.push_back(VecKey{10.0f, {10.0f, 0.0f, 0.0f}});
    clip.channels.push_back(channel);

    AnimationClip other = clip;
    other.name = "run";

    std::string error;
    fs::path skelPath(meshPath); skelPath.replace_extension(".dashskel");
    fs::path animPath(meshPath); animPath.replace_extension(".dashanim");
    if (!writeSkeleton(skelPath.string(), skeleton, error) ||
        !writeAnimationClips(animPath.string(), {clip, other}, error)) {
        std::fprintf(stderr, "fixture setup failed: %s\n", error.c_str());
        std::abort();
    }

    // Placeholder so the .dashmesh path itself exists on disk.
    fs::create_directories(dir);
    return meshPath;
}

static dash::vkexp::MeshPathResolver rigResolver()
{
    return [](const std::string& meshId) -> std::string {
        return meshId == "rig" ? bakedModelPath() : std::string{};
    };
}

static RenderInstance makeInstance(const std::string& meshId, const AnimationComponent* anim)
{
    RenderInstance inst;
    inst.meshId = meshId;
    if (anim) {
        inst.hasAnimation = true;
        inst.animation = *anim;
    }
    return inst;
}

// ─── Test: only entities carrying an AnimationComponent get a player ─────────
static void test_only_animated_instances_get_a_player()
{
    AnimationComponent anim;
    anim.clip = "walk";

    std::vector<RenderInstance> instances = {
        makeInstance("rig", nullptr),   // 0: no AnimationComponent
        makeInstance("rig", &anim),     // 1: animated
        makeInstance("cube", &anim),    // 2: animated but the model has no skeleton
    };

    AnimationSetCache cache;
    auto animators = buildAnimators(instances, cache, rigResolver());

    ASSERT_EQ(animators.size(), 1u);
    ASSERT_TRUE(animators.count(1) == 1);
    ASSERT_TRUE(animators.count(0) == 0);
    ASSERT_TRUE(animators.count(2) == 0);

    AnimationPlayer& player = animators.at(1);
    ASSERT_TRUE(player.skeleton() != nullptr);
    ASSERT_EQ(player.skeleton()->boneCount(), 2u);
    ASSERT_EQ(player.clipCount(), 2u);
    ASSERT_EQ(player.currentClipName(), std::string("walk"));
}

// ─── Test: the set is read once and shared by every instance of the model ────
static void test_animation_set_is_cached_per_model()
{
    AnimationComponent anim;
    anim.clip = "walk";

    std::vector<RenderInstance> instances;
    for (int i = 0; i < 4; ++i) instances.push_back(makeInstance("rig", &anim));

    int resolverCalls = 0;
    AnimationSetCache cache;
    auto animators = buildAnimators(instances, cache,
        [&resolverCalls](const std::string& meshId) -> std::string {
            ++resolverCalls;
            return meshId == "rig" ? bakedModelPath() : std::string{};
        });

    ASSERT_EQ(animators.size(), 4u);
    ASSERT_EQ(resolverCalls, 4);
    ASSERT_EQ(cache.size(), 1u);   // one entry for four instances

    // All four players share the same skeleton object, not four copies of it.
    const Skeleton* first = animators.at(0).skeleton();
    for (size_t i = 1; i < instances.size(); ++i) {
        ASSERT_TRUE(animators.at(i).skeleton() == first);
    }
}

// ─── Test: component fields land on the player ───────────────────────────────
static void test_sync_with_component_propagates_fields()
{
    AnimationComponent anim;
    anim.clip = "walk";
    anim.speed = 2.5f;
    anim.loop = false;
    anim.playing = false;
    anim.blendSeconds = 0.0f;

    std::vector<RenderInstance> instances = { makeInstance("rig", &anim) };
    AnimationSetCache cache;
    auto animators = buildAnimators(instances, cache, rigResolver());
    ASSERT_EQ(animators.size(), 1u);

    AnimationPlayer& player = animators.at(0);
    ASSERT_NEAR(player.speed(), 2.5f, 1e-6f);
    ASSERT_TRUE(!player.loop());
    ASSERT_TRUE(player.paused());
    ASSERT_EQ(player.currentClipName(), std::string("walk"));

    // Live edit from the Inspector: switching clip and resuming must stick.
    anim.clip = "run";
    anim.speed = 1.0f;
    anim.loop = true;
    anim.playing = true;
    player.syncWithComponent(anim);

    ASSERT_EQ(player.currentClipName(), std::string("run"));
    ASSERT_NEAR(player.speed(), 1.0f, 1e-6f);
    ASSERT_TRUE(player.loop());
    ASSERT_TRUE(!player.paused());

    // Clearing the clip drops back to bind pose.
    anim.clip.clear();
    player.syncWithComponent(anim);
    ASSERT_EQ(player.currentClipName(), std::string(""));
}

// ─── Test: paused instances hold their pose across updates ───────────────────
static void test_paused_instance_does_not_move()
{
    AnimationComponent anim;
    anim.clip = "walk";
    anim.playing = false;

    std::vector<RenderInstance> instances = { makeInstance("rig", &anim) };
    AnimationSetCache cache;
    auto animators = buildAnimators(instances, cache, rigResolver());

    AnimationPlayer& player = animators.at(0);
    player.update(0.0f);
    const std::vector<Mat4> before = player.boneMatrices();
    ASSERT_TRUE(!before.empty());

    for (int i = 0; i < 30; ++i) player.update(1.0f / 60.0f);

    const std::vector<Mat4>& after = player.boneMatrices();
    ASSERT_EQ(after.size(), before.size());
    for (size_t b = 0; b < before.size(); ++b) {
        for (int m = 0; m < 16; ++m) ASSERT_NEAR(after[b].m[m], before[b].m[m], 1e-6f);
    }
    ASSERT_NEAR(player.currentTimeSeconds(), 0.0f, 1e-6f);
}

// ─── Test: playing instances move once time advances ─────────────────────────
static void test_playing_instance_changes_bone_matrices()
{
    AnimationComponent anim;
    anim.clip = "walk";
    anim.playing = true;

    std::vector<RenderInstance> instances = { makeInstance("rig", &anim) };
    AnimationSetCache cache;
    auto animators = buildAnimators(instances, cache, rigResolver());

    AnimationPlayer& player = animators.at(0);
    player.update(0.0f);
    const std::vector<Mat4> before = player.boneMatrices();

    for (int i = 0; i < 30; ++i) player.update(1.0f / 60.0f);

    const std::vector<Mat4>& after = player.boneMatrices();
    ASSERT_EQ(after.size(), before.size());

    bool changed = false;
    for (size_t b = 0; b < before.size() && !changed; ++b) {
        for (int m = 0; m < 16; ++m) {
            if (std::fabs(after[b].m[m] - before[b].m[m]) > 1e-4f) { changed = true; break; }
        }
    }
    ASSERT_TRUE(changed);

    // 0.5 s at 10 ticks/s over a 10-tick clip: the child sits halfway on X.
    ASSERT_NEAR(player.currentTimeSeconds(), 0.5f, 1e-3f);
    ASSERT_NEAR(after[1].m[12], 5.0f, 1e-3f);
}

// ─── Test: a model with no .dashskel is skipped, not retried per instance ────
static void test_missing_skeleton_yields_no_animator()
{
    AnimationComponent anim;
    anim.clip = "walk";

    std::vector<RenderInstance> instances = {
        makeInstance("ghost", &anim),
        makeInstance("ghost", &anim),
    };

    const fs::path dir = fs::temp_directory_path() / "dash_test_anim_wiring";
    fs::create_directories(dir);
    const std::string ghostPath = (dir / "ghost.dashmesh").string();

    AnimationSetCache cache;
    auto animators = buildAnimators(instances, cache,
        [&ghostPath](const std::string&) { return ghostPath; });

    ASSERT_TRUE(animators.empty());
    ASSERT_EQ(cache.size(), 1u);   // the miss itself is cached
}

int main()
{
    std::printf("\n=== test_animation_wiring ===\n");

    RUN_TEST(test_only_animated_instances_get_a_player);
    RUN_TEST(test_animation_set_is_cached_per_model);
    RUN_TEST(test_sync_with_component_propagates_fields);
    RUN_TEST(test_paused_instance_does_not_move);
    RUN_TEST(test_playing_instance_changes_bone_matrices);
    RUN_TEST(test_missing_skeleton_yields_no_animator);

    std::printf("=== %d/%d passed ===\n\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
