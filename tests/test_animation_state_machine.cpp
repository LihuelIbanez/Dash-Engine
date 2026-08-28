// test_animation_state_machine.cpp — animation controller graph
//
// Covers: .animsm.json round-trip, condition evaluation, trigger consumption,
// "any state" transitions, the one-transition-per-frame guard, exit time and
// the AnimationPlayer integration (including that a player without a machine
// still behaves exactly as before). Everything is device-free: the state
// machine never touches Vulkan and the player only needs a two-bone skeleton.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/components/Components.h"
#include "rendering/animation/AnimationPlayer.h"
#include "rendering/animation/AnimationStateMachine.h"
#include "rendering/animation/AnimationStateMachineFile.h"

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

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

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
    static const fs::path dir = fs::temp_directory_path() / "dash_test_anim_sm";
    fs::create_directories(dir);
    return dir;
}

// ─── Fixtures ───────────────────────────────────────────────────────────────

// idle <-> walk on `speed`, plus "attack" reachable from any state on a trigger.
static AnimationStateMachine makeLocomotionMachine()
{
    AnimationStateMachine machine;
    machine.name = "wolf";
    machine.initialState = "idle";

    machine.parameters.push_back(AnimationParameter{"speed", ParamType::Float, 0.0f, false});
    machine.parameters.push_back(AnimationParameter{"grounded", ParamType::Bool, 0.0f, true});
    machine.parameters.push_back(AnimationParameter{"attack", ParamType::Trigger, 0.0f, false});

    machine.states.push_back(AnimationState{"idle", "Idle", 1.0f, true});
    machine.states.push_back(AnimationState{"walk", "Walk", 1.25f, true});
    machine.states.push_back(AnimationState{"attack", "Attack", 1.0f, false});

    AnimationTransition toWalk;
    toWalk.from = "idle";
    toWalk.to = "walk";
    toWalk.blendSeconds = 0.2f;
    toWalk.conditions.push_back(AnimationCondition{"speed", ConditionOp::Greater, 0.1f});
    toWalk.conditions.push_back(AnimationCondition{"grounded", ConditionOp::IsTrue, 0.0f});
    machine.transitions.push_back(toWalk);

    AnimationTransition toIdle;
    toIdle.from = "walk";
    toIdle.to = "idle";
    toIdle.blendSeconds = 0.15f;
    toIdle.conditions.push_back(AnimationCondition{"speed", ConditionOp::Less, 0.1f});
    machine.transitions.push_back(toIdle);

    AnimationTransition toAttack;
    toAttack.from.clear();            // any state
    toAttack.to = "attack";
    toAttack.blendSeconds = 0.05f;
    toAttack.conditions.push_back(AnimationCondition{"attack", ConditionOp::Triggered, 0.0f});
    machine.transitions.push_back(toAttack);

    AnimationTransition attackDone;
    attackDone.from = "attack";
    attackDone.to = "idle";
    attackDone.blendSeconds = 0.1f;
    attackDone.hasExitTime = true;
    attackDone.exitTime = 1.0f;
    machine.transitions.push_back(attackDone);

    return machine;
}

static std::shared_ptr<Skeleton> makeChainSkeleton()
{
    auto skeleton = std::make_shared<Skeleton>();
    Mat4 childBind = identity();
    childBind.m[13] = 1.0f;
    skeleton->addBone("root", -1, identity(), identity());
    skeleton->addBone("child", 0, identity(), childBind);
    return skeleton;
}

// 10 ticks at 10 ticks/second, i.e. exactly one second per playthrough.
static std::shared_ptr<const AnimationClip> makeClip(const std::string& name)
{
    AnimationChannel channel;
    channel.boneName = "child";
    channel.positions = {
        VecKey{0.0f,  {0.0f, 0.0f, 0.0f}},
        VecKey{10.0f, {1.0f, 0.0f, 0.0f}},
    };
    channel.rotations = {QuatKey{0.0f, Quat{}}};
    channel.scales = {VecKey{0.0f, {1.0f, 1.0f, 1.0f}}};

    auto clip = std::make_shared<AnimationClip>();
    clip->name = name;
    clip->duration = 10.0f;
    clip->ticksPerSecond = 10.0f;
    clip->channels.push_back(channel);
    return clip;
}

static AnimationPlayer makeLocomotionPlayer()
{
    AnimationPlayer player;
    player.setSkeleton(makeChainSkeleton());
    player.addClip(makeClip("Idle"));
    player.addClip(makeClip("Walk"));
    player.addClip(makeClip("Attack"));
    return player;
}

// ─── Test: .animsm.json round-trip keeps every field ────────────────────────
static void test_json_round_trip()
{
    const AnimationStateMachine source = makeLocomotionMachine();
    const fs::path path = tempDir() / "wolf.animsm.json";

    std::string error;
    ASSERT_TRUE(writeStateMachine(path.string(), source, error));

    AnimationStateMachine loaded;
    ASSERT_TRUE(readStateMachine(path.string(), loaded, error));

    ASSERT_EQ(loaded.name, source.name);
    ASSERT_EQ(loaded.initialState, source.initialState);
    ASSERT_EQ(loaded.parameters.size(), source.parameters.size());
    ASSERT_EQ(loaded.states.size(), source.states.size());
    ASSERT_EQ(loaded.transitions.size(), source.transitions.size());

    for (size_t i = 0; i < source.parameters.size(); ++i) {
        ASSERT_EQ(loaded.parameters[i].name, source.parameters[i].name);
        ASSERT_TRUE(loaded.parameters[i].type == source.parameters[i].type);
        ASSERT_NEAR(loaded.parameters[i].defaultFloat, source.parameters[i].defaultFloat, 1e-6f);
        ASSERT_EQ(loaded.parameters[i].defaultBool, source.parameters[i].defaultBool);
    }

    for (size_t i = 0; i < source.states.size(); ++i) {
        ASSERT_EQ(loaded.states[i].name, source.states[i].name);
        ASSERT_EQ(loaded.states[i].clip, source.states[i].clip);
        ASSERT_NEAR(loaded.states[i].speed, source.states[i].speed, 1e-6f);
        ASSERT_EQ(loaded.states[i].loop, source.states[i].loop);
    }

    for (size_t i = 0; i < source.transitions.size(); ++i) {
        const AnimationTransition& a = source.transitions[i];
        const AnimationTransition& b = loaded.transitions[i];
        ASSERT_EQ(b.from, a.from);
        ASSERT_EQ(b.to, a.to);
        ASSERT_NEAR(b.blendSeconds, a.blendSeconds, 1e-6f);
        ASSERT_EQ(b.hasExitTime, a.hasExitTime);
        ASSERT_NEAR(b.exitTime, a.exitTime, 1e-6f);
        ASSERT_EQ(b.conditions.size(), a.conditions.size());
        for (size_t c = 0; c < a.conditions.size(); ++c) {
            ASSERT_EQ(b.conditions[c].parameter, a.conditions[c].parameter);
            ASSERT_TRUE(b.conditions[c].op == a.conditions[c].op);
            ASSERT_NEAR(b.conditions[c].threshold, a.conditions[c].threshold, 1e-6f);
        }
    }

    // The empty "from" that means "any state" must survive the trip.
    ASSERT_TRUE(loaded.transitions[2].fromAnyState());
}

// ─── Test: malformed assets are rejected instead of half-loaded ─────────────
static void test_json_rejects_invalid_assets()
{
    std::string error;
    AnimationStateMachine out;

    ASSERT_FALSE(fromJson(nlohmann::json::array(), out, error));
    ASSERT_FALSE(error.empty());

    ASSERT_FALSE(fromJson(nlohmann::json::parse(R"({"states":[]})"), out, error));

    ASSERT_FALSE(fromJson(nlohmann::json::parse(
        R"({"states":[{"name":"a","clip":"A"}],"parameters":[{"name":"p","type":"vector"}]})"),
        out, error));

    ASSERT_FALSE(fromJson(nlohmann::json::parse(
        R"({"states":[{"name":"a","clip":"A"}],
            "transitions":[{"from":"a","to":"b","conditions":[{"parameter":"p","op":"between"}]}]})"),
        out, error));

    ASSERT_FALSE(fromJson(nlohmann::json::parse(R"({"version":99,"states":[{"name":"a"}]})"),
                          out, error));

    // Omitted optional fields fall back to the struct defaults.
    ASSERT_TRUE(fromJson(nlohmann::json::parse(R"({"states":[{"name":"a","clip":"A"}]})"),
                         out, error));
    ASSERT_EQ(out.states.size(), size_t{1});
    ASSERT_NEAR(out.states[0].speed, 1.0f, 1e-6f);
    ASSERT_TRUE(out.states[0].loop);
    ASSERT_EQ(out.entryState()->name, std::string("a"));
}

// ─── Test: a satisfied condition transitions, an unsatisfied one does not ───
static void test_conditions_gate_the_transition()
{
    StateMachineRuntime runtime;
    runtime.setMachine(std::make_shared<const AnimationStateMachine>(makeLocomotionMachine()));

    ASSERT_TRUE(runtime.active());
    ASSERT_EQ(runtime.currentState(), std::string("idle"));

    // First step is the entry into the initial state, not a transition.
    StateMachineRuntime::StepResult entry = runtime.step(0.0f);
    ASSERT_TRUE(entry.changed());
    ASSERT_EQ(entry.state->name, std::string("idle"));
    ASSERT_TRUE(entry.transition == nullptr);

    // speed still 0: nothing fires.
    ASSERT_FALSE(runtime.step(0.0f).changed());
    ASSERT_EQ(runtime.currentState(), std::string("idle"));

    // Above the threshold but not grounded: one condition of two fails.
    runtime.parameters().setFloat("speed", 3.0f);
    runtime.parameters().setBool("grounded", false);
    ASSERT_FALSE(runtime.step(0.0f).changed());
    ASSERT_EQ(runtime.currentState(), std::string("idle"));

    runtime.parameters().setBool("grounded", true);
    StateMachineRuntime::StepResult moved = runtime.step(0.0f);
    ASSERT_TRUE(moved.changed());
    ASSERT_EQ(moved.state->name, std::string("walk"));
    ASSERT_NEAR(moved.blendSeconds, 0.2f, 1e-6f);
    ASSERT_EQ(runtime.currentState(), std::string("walk"));

    // And back down again.
    runtime.parameters().setFloat("speed", 0.0f);
    ASSERT_TRUE(runtime.step(0.0f).changed());
    ASSERT_EQ(runtime.currentState(), std::string("idle"));
}

// ─── Test: every comparison operator, plus the undeclared-parameter case ────
static void test_condition_operators()
{
    ParameterBlock params;
    params.setFloat("hp", 42.0f);
    params.setBool("alive", true);
    params.setTrigger("hit");

    ASSERT_TRUE(evaluateCondition({"hp", ConditionOp::Greater, 41.0f}, params));
    ASSERT_FALSE(evaluateCondition({"hp", ConditionOp::Greater, 42.0f}, params));
    ASSERT_TRUE(evaluateCondition({"hp", ConditionOp::Less, 43.0f}, params));
    ASSERT_FALSE(evaluateCondition({"hp", ConditionOp::Less, 42.0f}, params));
    ASSERT_TRUE(evaluateCondition({"hp", ConditionOp::Equals, 42.0f}, params));
    ASSERT_FALSE(evaluateCondition({"hp", ConditionOp::NotEquals, 42.0f}, params));
    ASSERT_TRUE(evaluateCondition({"hp", ConditionOp::NotEquals, 7.0f}, params));
    ASSERT_TRUE(evaluateCondition({"alive", ConditionOp::IsTrue, 0.0f}, params));
    ASSERT_FALSE(evaluateCondition({"alive", ConditionOp::IsFalse, 0.0f}, params));
    ASSERT_TRUE(evaluateCondition({"hit", ConditionOp::Triggered, 0.0f}, params));

    // A bool is not a trigger, so `triggered` must not accept it.
    ASSERT_FALSE(evaluateCondition({"alive", ConditionOp::Triggered, 0.0f}, params));

    // A typo in the asset fails closed rather than reading as 0/false.
    ASSERT_FALSE(evaluateCondition({"hpp", ConditionOp::Less, 1000.0f}, params));
    ASSERT_FALSE(evaluateCondition({"hpp", ConditionOp::IsFalse, 0.0f}, params));
}

// ─── Test: a trigger fires once and stays consumed until set again ──────────
static void test_trigger_is_consumed_on_use()
{
    StateMachineRuntime runtime;
    runtime.setMachine(std::make_shared<const AnimationStateMachine>(makeLocomotionMachine()));
    runtime.step(0.0f);   // entry into idle

    runtime.parameters().setTrigger("attack");
    ASSERT_TRUE(runtime.parameters().isTriggerSet("attack"));

    ASSERT_TRUE(runtime.step(0.0f).changed());
    ASSERT_EQ(runtime.currentState(), std::string("attack"));
    ASSERT_FALSE(runtime.parameters().isTriggerSet("attack"));

    // Back to idle by exit time, then the stale trigger must not re-fire.
    ASSERT_TRUE(runtime.step(1.0f).changed());
    ASSERT_EQ(runtime.currentState(), std::string("idle"));
    ASSERT_FALSE(runtime.step(0.0f).changed());
    ASSERT_EQ(runtime.currentState(), std::string("idle"));

    // Setting it again fires again.
    runtime.parameters().setTrigger("attack");
    ASSERT_TRUE(runtime.step(0.0f).changed());
    ASSERT_EQ(runtime.currentState(), std::string("attack"));
}

// ─── Test: a pending trigger survives frames until something consumes it ────
static void test_trigger_survives_until_consumed()
{
    AnimationStateMachine machine;
    machine.initialState = "idle";
    machine.states.push_back(AnimationState{"idle", "Idle", 1.0f, true});
    machine.states.push_back(AnimationState{"attack", "Attack", 1.0f, false});
    machine.parameters.push_back(AnimationParameter{"attack", ParamType::Trigger, 0.0f, false});
    machine.parameters.push_back(AnimationParameter{"ready", ParamType::Bool, 0.0f, false});

    AnimationTransition t;
    t.from = "idle";
    t.to = "attack";
    t.conditions.push_back(AnimationCondition{"attack", ConditionOp::Triggered, 0.0f});
    t.conditions.push_back(AnimationCondition{"ready", ConditionOp::IsTrue, 0.0f});
    machine.transitions.push_back(t);

    StateMachineRuntime runtime;
    runtime.setMachine(std::make_shared<const AnimationStateMachine>(machine));
    runtime.step(0.0f);

    runtime.parameters().setTrigger("attack");
    for (int i = 0; i < 5; ++i) {
        ASSERT_FALSE(runtime.step(0.0f).changed());   // blocked by `ready`
        ASSERT_TRUE(runtime.parameters().isTriggerSet("attack"));
    }

    runtime.parameters().setBool("ready", true);
    ASSERT_TRUE(runtime.step(0.0f).changed());
    ASSERT_EQ(runtime.currentState(), std::string("attack"));
    ASSERT_FALSE(runtime.parameters().isTriggerSet("attack"));
}

// ─── Test: "any state" transitions fire from wherever the graph is ──────────
static void test_any_state_transition()
{
    auto machine = std::make_shared<const AnimationStateMachine>(makeLocomotionMachine());

    StateMachineRuntime runtime;
    runtime.setMachine(machine);
    runtime.step(0.0f);

    // From idle.
    runtime.parameters().setTrigger("attack");
    ASSERT_TRUE(runtime.step(0.0f).changed());
    ASSERT_EQ(runtime.currentState(), std::string("attack"));

    // From walk.
    runtime.reset();
    runtime.step(0.0f);
    runtime.parameters().setFloat("speed", 5.0f);
    runtime.step(0.0f);
    ASSERT_EQ(runtime.currentState(), std::string("walk"));

    runtime.parameters().setTrigger("attack");
    ASSERT_TRUE(runtime.step(0.0f).changed());
    ASSERT_EQ(runtime.currentState(), std::string("attack"));

    // An "any state" transition must not loop back into its own destination.
    runtime.parameters().setTrigger("attack");
    ASSERT_FALSE(runtime.step(0.0f).changed());
    ASSERT_EQ(runtime.currentState(), std::string("attack"));
}

// ─── Test: at most one transition per frame even with a satisfied chain ─────
static void test_one_transition_per_frame()
{
    AnimationStateMachine machine;
    machine.initialState = "a";
    machine.parameters.push_back(AnimationParameter{"go", ParamType::Bool, 0.0f, true});

    const char* names[] = {"a", "b", "c", "d"};
    for (const char* name : names) {
        machine.states.push_back(AnimationState{name, std::string("clip_") + name, 1.0f, true});
    }
    for (int i = 0; i + 1 < 4; ++i) {
        AnimationTransition t;
        t.from = names[i];
        t.to = names[i + 1];
        t.blendSeconds = 0.0f;
        t.conditions.push_back(AnimationCondition{"go", ConditionOp::IsTrue, 0.0f});
        machine.transitions.push_back(t);
    }

    StateMachineRuntime runtime;
    runtime.setMachine(std::make_shared<const AnimationStateMachine>(machine));

    runtime.step(0.0f);                                       // entry
    ASSERT_EQ(runtime.currentState(), std::string("a"));
    ASSERT_EQ(runtime.step(0.0f).state->name, std::string("b"));
    ASSERT_EQ(runtime.step(0.0f).state->name, std::string("c"));
    ASSERT_EQ(runtime.step(0.0f).state->name, std::string("d"));
    ASSERT_FALSE(runtime.step(0.0f).changed());               // end of the chain

    // Same guarantee through the player: one update, one state change.
    AnimationPlayer player;
    player.setSkeleton(makeChainSkeleton());
    for (const char* name : names) player.addClip(makeClip(std::string("clip_") + name));
    player.setStateMachine(std::make_shared<const AnimationStateMachine>(machine));

    player.update(0.016f);
    ASSERT_EQ(player.currentClipName(), std::string("clip_a"));
    player.update(0.016f);
    ASSERT_EQ(player.currentClipName(), std::string("clip_b"));
    player.update(0.016f);
    ASSERT_EQ(player.currentClipName(), std::string("clip_c"));
}

// ─── Test: hasExitTime holds a state until the clip has played through ──────
static void test_exit_time_holds_the_state()
{
    AnimationPlayer player = makeLocomotionPlayer();
    player.setStateMachine(std::make_shared<const AnimationStateMachine>(makeLocomotionMachine()));

    player.update(0.1f);                       // entry into idle
    ASSERT_EQ(player.currentClipName(), std::string("Idle"));

    player.stateMachine().parameters().setTrigger("attack");
    player.update(0.1f);
    ASSERT_EQ(player.stateMachine().currentState(), std::string("attack"));
    ASSERT_EQ(player.currentClipName(), std::string("Attack"));

    // Attack runs for one second; it must not bail out halfway.
    for (int i = 0; i < 9; ++i) {
        player.update(0.1f);
        ASSERT_EQ(player.stateMachine().currentState(), std::string("attack"));
    }

    player.update(0.1f);
    ASSERT_EQ(player.stateMachine().currentState(), std::string("idle"));
    ASSERT_EQ(player.currentClipName(), std::string("Idle"));
}

// ─── Test: the state's clip, speed and loop reach the player ────────────────
static void test_player_adopts_state_playback_settings()
{
    AnimationPlayer player = makeLocomotionPlayer();
    player.setStateMachine(std::make_shared<const AnimationStateMachine>(makeLocomotionMachine()));

    player.update(0.016f);
    ASSERT_EQ(player.currentClipName(), std::string("Idle"));
    ASSERT_NEAR(player.speed(), 1.0f, 1e-6f);
    ASSERT_TRUE(player.loop());

    player.stateMachine().parameters().setFloat("speed", 4.0f);
    player.update(0.016f);
    ASSERT_EQ(player.currentClipName(), std::string("Walk"));
    ASSERT_NEAR(player.speed(), 1.25f, 1e-6f);
    ASSERT_TRUE(player.isBlending());   // 0.2s blend requested by the transition

    player.stateMachine().parameters().setTrigger("attack");
    player.update(0.016f);
    ASSERT_EQ(player.currentClipName(), std::string("Attack"));
    ASSERT_FALSE(player.loop());
}

// ─── Test: the machine wins over AnimationComponent::clip while installed ───
static void test_machine_overrides_component_clip()
{
    AnimationPlayer player = makeLocomotionPlayer();
    player.setStateMachine(std::make_shared<const AnimationStateMachine>(makeLocomotionMachine()));

    AnimationComponent component;
    component.clip = "Walk";              // stale authored value
    component.stateMachine = "wolf.animsm.json";
    component.speed = 9.0f;

    player.syncWithComponent(component);
    player.update(0.016f);
    ASSERT_EQ(player.currentClipName(), std::string("Idle"));
    ASSERT_NEAR(player.speed(), 1.0f, 1e-6f);

    // Pausing still works: the component owns transport, not the graph.
    component.playing = false;
    player.stateMachine().parameters().setFloat("speed", 4.0f);
    player.syncWithComponent(component);
    player.update(0.016f);
    ASSERT_EQ(player.stateMachine().currentState(), std::string("idle"));

    component.playing = true;
    player.syncWithComponent(component);
    player.update(0.016f);
    ASSERT_EQ(player.stateMachine().currentState(), std::string("walk"));

    // Detaching the controller gives the component its authority back.
    player.setStateMachine(nullptr);
    ASSERT_FALSE(player.stateMachineActive());
    player.syncWithComponent(component);
    ASSERT_EQ(player.currentClipName(), std::string("Walk"));
    ASSERT_NEAR(player.speed(), 9.0f, 1e-6f);
}

// ─── Test: no machine installed -> exactly the previous behaviour ───────────
static void test_empty_state_machine_keeps_legacy_behaviour()
{
    AnimationPlayer player = makeLocomotionPlayer();
    ASSERT_FALSE(player.stateMachineActive());

    AnimationComponent component;
    component.clip = "Idle";
    component.speed = 2.0f;
    component.loop = false;
    ASSERT_TRUE(component.stateMachine.empty());

    player.syncWithComponent(component);
    ASSERT_EQ(player.currentClipName(), std::string("Idle"));
    ASSERT_NEAR(player.speed(), 2.0f, 1e-6f);
    ASSERT_FALSE(player.loop());

    // Ticking never changes the clip on its own.
    for (int i = 0; i < 20; ++i) {
        player.syncWithComponent(component);
        player.update(0.05f);
    }
    ASSERT_EQ(player.currentClipName(), std::string("Idle"));

    // Editing the component still drives the clip and starts a crossfade.
    component.clip = "Walk";
    component.blendSeconds = 0.3f;
    player.syncWithComponent(component);
    ASSERT_EQ(player.currentClipName(), std::string("Walk"));
    ASSERT_TRUE(player.isBlending());

    component.clip.clear();
    player.syncWithComponent(component);
    ASSERT_TRUE(player.currentClipName().empty());
}

// ─── Test: reset() rewinds to the initial state and reseeds defaults ────────
static void test_reset_restores_defaults()
{
    StateMachineRuntime runtime;
    runtime.setMachine(std::make_shared<const AnimationStateMachine>(makeLocomotionMachine()));
    runtime.step(0.0f);

    ASSERT_TRUE(runtime.parameters().getBool("grounded"));   // default true
    ASSERT_NEAR(runtime.parameters().getFloat("speed"), 0.0f, 1e-6f);
    ASSERT_FALSE(runtime.parameters().isTriggerSet("attack"));

    runtime.parameters().setFloat("speed", 7.0f);
    runtime.step(0.0f);
    ASSERT_EQ(runtime.currentState(), std::string("walk"));

    runtime.reset();
    ASSERT_EQ(runtime.currentState(), std::string("idle"));
    ASSERT_NEAR(runtime.parameters().getFloat("speed"), 0.0f, 1e-6f);

    // setState() jumps without asking the conditions.
    ASSERT_TRUE(runtime.setState("attack"));
    ASSERT_EQ(runtime.currentState(), std::string("attack"));
    ASSERT_FALSE(runtime.setState("nope"));

    runtime.clearMachine();
    ASSERT_FALSE(runtime.active());
    ASSERT_FALSE(runtime.step(0.0f).changed());
}

int main()
{
    std::printf("Running animation state machine tests...\n");

    RUN_TEST(test_json_round_trip);
    RUN_TEST(test_json_rejects_invalid_assets);
    RUN_TEST(test_conditions_gate_the_transition);
    RUN_TEST(test_condition_operators);
    RUN_TEST(test_trigger_is_consumed_on_use);
    RUN_TEST(test_trigger_survives_until_consumed);
    RUN_TEST(test_any_state_transition);
    RUN_TEST(test_one_transition_per_frame);
    RUN_TEST(test_exit_time_holds_the_state);
    RUN_TEST(test_player_adopts_state_playback_settings);
    RUN_TEST(test_machine_overrides_component_clip);
    RUN_TEST(test_empty_state_machine_keeps_legacy_behaviour);
    RUN_TEST(test_reset_restores_defaults);

    std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
