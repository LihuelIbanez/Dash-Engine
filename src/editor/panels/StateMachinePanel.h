#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// StateMachinePanel — author .animsm.json controllers (the "movement editor")
//
// Everything that decides something (validation against the clips a model
// actually ships, reachability of the graph, and the renames/removals that have
// to be propagated to transitions and conditions) lives in
// dash::editor::animsm: header-only and ImGui-free, so the tests can exercise
// it headless.
// ─────────────────────────────────────────────────────────────────────────────

#include "rendering/animation/AnimationStateMachine.h"
#include "rendering/animation/AnimationStateMachineFile.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace dash::editor::animsm {

using dash::anim::AnimationCondition;
using dash::anim::AnimationParameter;
using dash::anim::AnimationState;
using dash::anim::AnimationStateMachine;
using dash::anim::AnimationTransition;
using dash::anim::ConditionOp;
using dash::anim::ParamType;

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

inline bool hasState(const AnimationStateMachine& machine, const std::string& name)
{
    return machine.findState(name) != nullptr;
}

inline bool hasParameter(const AnimationStateMachine& machine, const std::string& name)
{
    return machine.findParameter(name) != nullptr;
}

inline bool hasClip(const std::vector<std::string>& clips, const std::string& clip)
{
    return std::find(clips.begin(), clips.end(), clip) != clips.end();
}

/// Which condition ops actually read the field the parameter stores. An `isTrue`
/// on a float reads a bool that is never written, so the transition can never
/// open — worth reporting even though the file parses.
inline bool opMatchesType(ConditionOp op, ParamType type)
{
    switch (type) {
        case ParamType::Float:
            return op == ConditionOp::Greater || op == ConditionOp::Less ||
                   op == ConditionOp::Equals  || op == ConditionOp::NotEquals;
        case ParamType::Bool:
            return op == ConditionOp::IsTrue || op == ConditionOp::IsFalse;
        case ParamType::Trigger:
            return op == ConditionOp::Triggered;
    }
    return false;
}

/// States the runtime can actually end up in, walking from the entry state. A
/// transition with an empty `from` fires from everywhere, so its target is
/// reachable as soon as the machine has an entry at all.
inline std::vector<std::string> reachableStates(const AnimationStateMachine& machine)
{
    std::vector<std::string> reached;
    const AnimationState* entry = machine.entryState();
    if (!entry) return reached;

    std::vector<std::string> frontier;
    auto visit = [&](const std::string& name) {
        if (!hasState(machine, name)) return;
        if (std::find(reached.begin(), reached.end(), name) != reached.end()) return;
        reached.push_back(name);
        frontier.push_back(name);
    };

    visit(entry->name);
    for (const AnimationTransition& transition : machine.transitions) {
        if (transition.fromAnyState()) visit(transition.to);
    }

    while (!frontier.empty()) {
        const std::string current = frontier.back();
        frontier.pop_back();
        for (const AnimationTransition& transition : machine.transitions) {
            if (transition.from == current) visit(transition.to);
        }
    }
    return reached;
}

inline std::vector<int> transitionsFrom(const AnimationStateMachine& machine,
                                        const std::string& stateName)
{
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(machine.transitions.size()); ++i) {
        if (machine.transitions[static_cast<std::size_t>(i)].from == stateName)
            out.push_back(i);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Validation
// ─────────────────────────────────────────────────────────────────────────────

enum class IssueSeverity { Warning, Error };

struct Issue {
    IssueSeverity severity = IssueSeverity::Error;
    std::string   message;
};

/// `availableClips` empty means "the clip list is unknown", and clip names are
/// then left unchecked instead of every state being reported as broken.
inline std::vector<Issue> validate(const AnimationStateMachine& machine,
                                   const std::vector<std::string>& availableClips)
{
    std::vector<Issue> issues;
    const auto error = [&](std::string text) {
        issues.push_back({IssueSeverity::Error, std::move(text)});
    };
    const auto warning = [&](std::string text) {
        issues.push_back({IssueSeverity::Warning, std::move(text)});
    };

    if (machine.states.empty()) {
        error("the machine has no states");
        return issues;
    }

    for (std::size_t i = 0; i < machine.states.size(); ++i) {
        const AnimationState& state = machine.states[i];
        if (state.name.empty()) {
            error("state #" + std::to_string(i) + " has an empty name");
            continue;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (machine.states[j].name != state.name) continue;
            error("duplicate state name '" + state.name + "'");
            break;
        }
        if (state.clip.empty()) {
            warning("state '" + state.name + "' has no clip: it will hold the bind pose");
        } else if (!availableClips.empty() && !hasClip(availableClips, state.clip)) {
            error("state '" + state.name + "' references clip '" + state.clip +
                  "', which the model does not ship");
        }
        if (state.speed == 0.0f)
            warning("state '" + state.name + "' has speed 0: the clip will not advance");
    }

    for (std::size_t i = 0; i < machine.parameters.size(); ++i) {
        const AnimationParameter& param = machine.parameters[i];
        if (param.name.empty()) {
            error("parameter #" + std::to_string(i) + " has an empty name");
            continue;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (machine.parameters[j].name != param.name) continue;
            error("duplicate parameter name '" + param.name + "'");
            break;
        }
    }

    if (machine.initialState.empty()) {
        error("no initial state is set");
    } else if (!hasState(machine, machine.initialState)) {
        error("initial state '" + machine.initialState + "' does not exist");
    }

    for (std::size_t i = 0; i < machine.transitions.size(); ++i) {
        const AnimationTransition& transition = machine.transitions[i];
        const std::string label = "transition #" + std::to_string(i) + " (" +
                                  (transition.fromAnyState() ? std::string("Any State")
                                                             : transition.from) +
                                  " -> " + transition.to + ")";

        if (transition.to.empty())
            error(label + " has no destination state");
        else if (!hasState(machine, transition.to))
            error(label + " targets state '" + transition.to + "', which does not exist");

        if (!transition.fromAnyState() && !hasState(machine, transition.from))
            error(label + " starts from state '" + transition.from + "', which does not exist");

        if (!transition.fromAnyState() && transition.from == transition.to)
            warning(label + " loops onto itself and is skipped at runtime");

        if (transition.conditions.empty() && !transition.hasExitTime)
            warning(label + " has no conditions and no exit time: it fires immediately");

        for (const AnimationCondition& condition : transition.conditions) {
            const AnimationParameter* param = machine.findParameter(condition.parameter);
            if (!param) {
                error(label + " tests parameter '" + condition.parameter +
                      "', which is not declared");
                continue;
            }
            if (!opMatchesType(condition.op, param->type)) {
                warning(label + " uses '" + dash::anim::toString(condition.op) +
                        "' on the " + dash::anim::toString(param->type) + " parameter '" +
                        param->name + "': the condition can never pass");
            }
        }
    }

    const std::vector<std::string> reached = reachableStates(machine);
    for (const AnimationState& state : machine.states) {
        if (std::find(reached.begin(), reached.end(), state.name) != reached.end()) continue;
        warning("state '" + state.name + "' is unreachable: no transition leads to it");
    }

    return issues;
}

inline bool hasErrors(const std::vector<Issue>& issues)
{
    for (const Issue& issue : issues)
        if (issue.severity == IssueSeverity::Error) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mutations that have to touch more than one list
// ─────────────────────────────────────────────────────────────────────────────

/// "name", "name 2", "name 3"… so adding a state twice cannot silently produce
/// a duplicate the validator then flags.
inline std::string uniqueName(const std::vector<std::string>& taken, const std::string& base)
{
    if (std::find(taken.begin(), taken.end(), base) == taken.end()) return base;
    for (int suffix = 2; suffix < 1000; ++suffix) {
        const std::string candidate = base + " " + std::to_string(suffix);
        if (std::find(taken.begin(), taken.end(), candidate) == taken.end()) return candidate;
    }
    return base;
}

inline std::vector<std::string> stateNames(const AnimationStateMachine& machine)
{
    std::vector<std::string> names;
    names.reserve(machine.states.size());
    for (const AnimationState& state : machine.states) names.push_back(state.name);
    return names;
}

inline std::vector<std::string> parameterNames(const AnimationStateMachine& machine)
{
    std::vector<std::string> names;
    names.reserve(machine.parameters.size());
    for (const AnimationParameter& param : machine.parameters) names.push_back(param.name);
    return names;
}

inline void renameState(AnimationStateMachine& machine, int index, const std::string& newName)
{
    if (index < 0 || index >= static_cast<int>(machine.states.size())) return;
    if (newName.empty()) return;

    const std::string oldName = machine.states[static_cast<std::size_t>(index)].name;
    if (oldName == newName) return;

    machine.states[static_cast<std::size_t>(index)].name = newName;
    if (machine.initialState == oldName) machine.initialState = newName;
    for (AnimationTransition& transition : machine.transitions) {
        if (transition.from == oldName) transition.from = newName;
        if (transition.to == oldName)   transition.to = newName;
    }
}

inline void renameParameter(AnimationStateMachine& machine, int index, const std::string& newName)
{
    if (index < 0 || index >= static_cast<int>(machine.parameters.size())) return;
    if (newName.empty()) return;

    const std::string oldName = machine.parameters[static_cast<std::size_t>(index)].name;
    if (oldName == newName) return;

    machine.parameters[static_cast<std::size_t>(index)].name = newName;
    for (AnimationTransition& transition : machine.transitions)
        for (AnimationCondition& condition : transition.conditions)
            if (condition.parameter == oldName) condition.parameter = newName;
}

/// Drops the state and every transition that mentioned it, so removing a node
/// cannot leave a dangling edge behind.
inline void removeState(AnimationStateMachine& machine, int index)
{
    if (index < 0 || index >= static_cast<int>(machine.states.size())) return;
    const std::string name = machine.states[static_cast<std::size_t>(index)].name;

    machine.states.erase(machine.states.begin() + index);
    machine.transitions.erase(
        std::remove_if(machine.transitions.begin(), machine.transitions.end(),
                       [&](const AnimationTransition& t) {
                           return t.to == name || (!t.fromAnyState() && t.from == name);
                       }),
        machine.transitions.end());

    if (machine.initialState == name)
        machine.initialState = machine.states.empty() ? std::string{} : machine.states.front().name;
}

inline void removeParameter(AnimationStateMachine& machine, int index)
{
    if (index < 0 || index >= static_cast<int>(machine.parameters.size())) return;
    const std::string name = machine.parameters[static_cast<std::size_t>(index)].name;

    machine.parameters.erase(machine.parameters.begin() + index);
    for (AnimationTransition& transition : machine.transitions) {
        transition.conditions.erase(
            std::remove_if(transition.conditions.begin(), transition.conditions.end(),
                           [&](const AnimationCondition& c) { return c.parameter == name; }),
            transition.conditions.end());
    }
}

} // namespace dash::editor::animsm

// ─────────────────────────────────────────────────────────────────────────────
// StateMachinePanel — the ImGui window itself
//
// Autonomous: draw() opens and closes its own window and owns the machine it is
// editing. It touches the .animsm.json on disk and reads clip names out of a
// .dashanim; it never mutates the scene.
// ─────────────────────────────────────────────────────────────────────────────
class StateMachinePanel {
public:
    using LogCallback = std::function<void(const std::string&)>;

    void draw(const std::string& assetsRoot, LogCallback logCb = nullptr);

    bool load(const std::string& path, std::string& outError);
    bool save(const std::string& path, std::string& outError);
    bool loadClipSource(const std::string& dashanimPath, std::string& outError);

    const std::string& loadedPath() const { return path_; }
    const dash::anim::AnimationStateMachine& machine() const { return machine_; }

private:
    void refreshIssues();
    void newMachine();
    void resetSimulation();

    void drawSourceBar(const std::string& assetsRoot, LogCallback& logCb);
    void drawParameters();
    void drawStates();
    void drawTransitions();
    void drawIssues();
    void drawSimulation();

    dash::anim::AnimationStateMachine machine_;
    std::string                       path_;
    bool                              dirty_ = false;

    std::vector<dash::editor::animsm::Issue> issues_;

    // Clip names come from a .dashanim so states can only pick clips that exist.
    std::string              clipSourcePath_;
    std::vector<std::string> clips_;

    int selectedState_      = -1;
    int selectedParameter_  = -1;
    int selectedTransition_ = -1;

    char pathBuf_[512]      = {0};
    char clipPathBuf_[512]  = {0};
    char nameBuf_[128]      = {0};
    char stateNameBuf_[128] = {0};
    char paramNameBuf_[128] = {0};

    std::vector<std::string> foundMachines_;
    std::vector<std::string> foundClipFiles_;
    bool                     scanned_ = false;

    // Simulation: drives a real StateMachineRuntime off hand-set parameters.
    dash::anim::StateMachineRuntime sim_;
    bool  simActive_ = false;
    float simNormalizedTime_ = 1.0f;
    bool  simAutoStep_ = false;

    std::string status_;
    bool        statusIsError_ = false;
};
