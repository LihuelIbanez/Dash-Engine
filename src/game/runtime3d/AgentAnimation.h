#pragma once

#include <memory>
#include <string>

#include "game/runtime3d/AgentAI.h"
#include "rendering/animation/AnimationStateMachine.h"

// ─────────────────────────────────────────────────────────────────────────────
// AgentAnimation — bridge between the combat FSM and the animation graph.
//
// Still pure (no Vulkan, no World): it only produces an AnimationStateMachine
// asset and writes ParameterBlock values, so it is testable headless.
// ─────────────────────────────────────────────────────────────────────────────

namespace dash::runtime3d {

inline constexpr const char* kAnimParamSpeed  = "speed";
inline constexpr const char* kAnimParamAttack = "attack";
inline constexpr const char* kAnimParamDie    = "die";

// A locomotion state is considered "moving" above this planar speed.
inline constexpr float kAnimMoveThreshold = 0.15f;

// Nominal clip length used to normalise playback progress when nothing is
// actually sampling a clip (enemies are drawn as cubes today, see report).
inline constexpr float kAnimNominalClipSeconds = 0.8f;

// Fallback locomotion graph so the FSM always has something to drive even when
// the scene ships no .animsm.json. Clip names follow the import convention.
inline dash::anim::AnimationStateMachine defaultEnemyStateMachine()
{
    using namespace dash::anim;

    AnimationStateMachine machine;
    machine.name = "EnemyLocomotion";
    machine.initialState = "Idle";

    machine.parameters.push_back({kAnimParamSpeed,  ParamType::Float,   0.0f, false});
    machine.parameters.push_back({kAnimParamAttack, ParamType::Trigger, 0.0f, false});
    machine.parameters.push_back({kAnimParamDie,    ParamType::Trigger, 0.0f, false});

    machine.states.push_back({"Idle",   "Idle",   1.0f, true});
    machine.states.push_back({"Walk",   "Walk",   1.0f, true});
    machine.states.push_back({"Attack", "Attack", 1.0f, false});
    machine.states.push_back({"Death",  "Death",  1.0f, false});

    // Death first: it has to win over anything else pending this frame. Listed
    // per source state instead of any-state so a corpse never leaves Death.
    for (const char* from : {"Idle", "Walk", "Attack"}) {
        AnimationTransition die;
        die.from = from;
        die.to = "Death";
        die.conditions.push_back({kAnimParamDie, ConditionOp::Triggered, 0.0f});
        die.blendSeconds = 0.1f;
        machine.transitions.push_back(die);
    }

    for (const char* from : {"Idle", "Walk"}) {
        AnimationTransition swing;
        swing.from = from;
        swing.to = "Attack";
        swing.conditions.push_back({kAnimParamAttack, ConditionOp::Triggered, 0.0f});
        swing.blendSeconds = 0.08f;
        machine.transitions.push_back(swing);
    }

    AnimationTransition swingToWalk;
    swingToWalk.from = "Attack";
    swingToWalk.to = "Walk";
    swingToWalk.conditions.push_back({kAnimParamSpeed, ConditionOp::Greater, kAnimMoveThreshold});
    swingToWalk.hasExitTime = true;
    swingToWalk.blendSeconds = 0.15f;
    machine.transitions.push_back(swingToWalk);

    AnimationTransition swingToIdle;
    swingToIdle.from = "Attack";
    swingToIdle.to = "Idle";
    swingToIdle.conditions.push_back({kAnimParamSpeed, ConditionOp::Less, kAnimMoveThreshold});
    swingToIdle.hasExitTime = true;
    swingToIdle.blendSeconds = 0.15f;
    machine.transitions.push_back(swingToIdle);

    AnimationTransition start;
    start.from = "Idle";
    start.to = "Walk";
    start.conditions.push_back({kAnimParamSpeed, ConditionOp::Greater, kAnimMoveThreshold});
    start.blendSeconds = 0.15f;
    machine.transitions.push_back(start);

    AnimationTransition stop;
    stop.from = "Walk";
    stop.to = "Idle";
    stop.conditions.push_back({kAnimParamSpeed, ConditionOp::Less, kAnimMoveThreshold});
    stop.blendSeconds = 0.2f;
    machine.transitions.push_back(stop);

    return machine;
}

// The asset is immutable and shared by every agent; only the parameter values
// and the current state are per instance.
inline std::shared_ptr<const dash::anim::AnimationStateMachine> sharedEnemyStateMachine()
{
    static const std::shared_ptr<const dash::anim::AnimationStateMachine> machine =
        std::make_shared<const dash::anim::AnimationStateMachine>(defaultEnemyStateMachine());
    return machine;
}

struct AgentAnimSignals {
    float speed = 0.0f;          // planar speed, tiles per second
    bool  attackStarted = false; // a swing landed this frame
    bool  died = false;          // health hit zero this frame
};

inline void applyAgentAnimation(const AgentAnimSignals& signals,
                                dash::anim::ParameterBlock& params)
{
    params.setFloat(kAnimParamSpeed, signals.speed);
    if (signals.attackStarted) params.setTrigger(kAnimParamAttack);
    if (signals.died)          params.setTrigger(kAnimParamDie);
}

// Locomotion intent of a combat state, used to sanity-check the wiring without
// a renderer: Chase/Flee/Patrol walk, Attack and Idle stand, Dead is inert.
inline bool stateWalks(AgentState state)
{
    return state == AgentState::Chase || state == AgentState::Flee ||
           state == AgentState::Patrol;
}

} // namespace dash::runtime3d
