#pragma once

// Header-only for the same reason as Skeleton.h / AnimationWiring.h: the root
// CMakeLists lists the animation sources one by one, so a new .cpp here would
// silently never be compiled into vulkan_experimental.
//
// Pure data + evaluation: no Vulkan, no JSON, no file I/O. Serialization lives
// in AnimationStateMachineFile.h so the renderer never pulls in the parser.

#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace dash::anim {

enum class ParamType {
    Float,
    Bool,
    Trigger,   // a bool that is cleared by the transition that consumes it
};

enum class ConditionOp {
    Greater,
    Less,
    Equals,
    NotEquals,
    IsTrue,
    IsFalse,
    Triggered,
};

inline const char* toString(ParamType type)
{
    switch (type) {
        case ParamType::Bool:    return "bool";
        case ParamType::Trigger: return "trigger";
        case ParamType::Float:   break;
    }
    return "float";
}

inline bool parseParamType(const std::string& text, ParamType& out)
{
    if (text == "float")   { out = ParamType::Float;   return true; }
    if (text == "bool")    { out = ParamType::Bool;    return true; }
    if (text == "trigger") { out = ParamType::Trigger; return true; }
    return false;
}

inline const char* toString(ConditionOp op)
{
    switch (op) {
        case ConditionOp::Less:      return "less";
        case ConditionOp::Equals:    return "equals";
        case ConditionOp::NotEquals: return "notEquals";
        case ConditionOp::IsTrue:    return "isTrue";
        case ConditionOp::IsFalse:   return "isFalse";
        case ConditionOp::Triggered: return "triggered";
        case ConditionOp::Greater:   break;
    }
    return "greater";
}

inline bool parseConditionOp(const std::string& text, ConditionOp& out)
{
    if (text == "greater")   { out = ConditionOp::Greater;   return true; }
    if (text == "less")      { out = ConditionOp::Less;      return true; }
    if (text == "equals")    { out = ConditionOp::Equals;    return true; }
    if (text == "notEquals") { out = ConditionOp::NotEquals; return true; }
    if (text == "isTrue")    { out = ConditionOp::IsTrue;    return true; }
    if (text == "isFalse")   { out = ConditionOp::IsFalse;   return true; }
    if (text == "triggered") { out = ConditionOp::Triggered; return true; }
    return false;
}

// Declaration of a parameter plus the value it is seeded with on reset.
struct AnimationParameter {
    std::string name;
    ParamType   type = ParamType::Float;
    float       defaultFloat = 0.0f;
    bool        defaultBool = false;
};

// One node of the graph: which clip plays and how while the state is current.
struct AnimationState {
    std::string name;
    std::string clip;
    float       speed = 1.0f;
    bool        loop = true;
};

struct AnimationCondition {
    std::string parameter;
    ConditionOp op = ConditionOp::Greater;
    float       threshold = 0.0f;
};

struct AnimationTransition {
    std::string from;                 // empty = "any state"
    std::string to;
    std::vector<AnimationCondition> conditions;
    float blendSeconds = 0.2f;
    // Gate on playback progress so an attack can finish before leaving.
    bool  hasExitTime = false;
    float exitTime = 1.0f;            // 1.0 = one full clip playthrough

    bool fromAnyState() const { return from.empty(); }
};

struct AnimationStateMachine {
    std::string name;
    std::string initialState;
    std::vector<AnimationParameter>  parameters;
    std::vector<AnimationState>      states;
    std::vector<AnimationTransition> transitions;

    const AnimationState* findState(const std::string& stateName) const
    {
        for (const AnimationState& state : states) {
            if (state.name == stateName) return &state;
        }
        return nullptr;
    }

    const AnimationParameter* findParameter(const std::string& paramName) const
    {
        for (const AnimationParameter& param : parameters) {
            if (param.name == paramName) return &param;
        }
        return nullptr;
    }

    bool empty() const { return states.empty(); }

    // Falls back to the first state so a machine that forgot `initialState`
    // still runs instead of sitting in bind pose.
    const AnimationState* entryState() const
    {
        if (const AnimationState* state = findState(initialState)) return state;
        return states.empty() ? nullptr : &states.front();
    }
};

// Live parameter values. Kept apart from the machine asset because the asset is
// shared (const) by every instance while the values are per instance.
class ParameterBlock {
public:
    struct Value {
        ParamType type = ParamType::Float;
        float     floatValue = 0.0f;
        bool      boolValue = false;   // also the pending flag of a trigger
    };

    void setFloat(const std::string& name, float value)
    {
        Value& v = values_[name];
        v.type = ParamType::Float;
        v.floatValue = value;
    }

    void setBool(const std::string& name, bool value)
    {
        Value& v = values_[name];
        v.type = ParamType::Bool;
        v.boolValue = value;
    }

    // Stays pending across frames until a transition consumes it.
    void setTrigger(const std::string& name)
    {
        Value& v = values_[name];
        v.type = ParamType::Trigger;
        v.boolValue = true;
    }

    void resetTrigger(const std::string& name)
    {
        auto it = values_.find(name);
        if (it != values_.end()) it->second.boolValue = false;
    }

    float getFloat(const std::string& name) const
    {
        const Value* v = find(name);
        return v ? v->floatValue : 0.0f;
    }

    bool getBool(const std::string& name) const
    {
        const Value* v = find(name);
        return v && v->boolValue;
    }

    bool isTriggerSet(const std::string& name) const
    {
        const Value* v = find(name);
        return v && v->type == ParamType::Trigger && v->boolValue;
    }

    bool has(const std::string& name) const { return find(name) != nullptr; }
    size_t size() const { return values_.size(); }
    void clear() { values_.clear(); }

    void resetToDefaults(const AnimationStateMachine& machine)
    {
        values_.clear();
        for (const AnimationParameter& param : machine.parameters) {
            Value& v = values_[param.name];
            v.type = param.type;
            v.floatValue = param.defaultFloat;
            v.boolValue = param.type == ParamType::Trigger ? false : param.defaultBool;
        }
    }

    const Value* find(const std::string& name) const
    {
        auto it = values_.find(name);
        return it == values_.end() ? nullptr : &it->second;
    }

private:
    std::unordered_map<std::string, Value> values_;
};

// An undeclared parameter fails the condition instead of defaulting to 0/false,
// so a typo in the asset cannot silently open a transition.
inline bool evaluateCondition(const AnimationCondition& condition, const ParameterBlock& params)
{
    const ParameterBlock::Value* value = params.find(condition.parameter);
    if (!value) return false;

    constexpr float kEpsilon = 1e-4f;
    switch (condition.op) {
        case ConditionOp::Greater:   return value->floatValue > condition.threshold;
        case ConditionOp::Less:      return value->floatValue < condition.threshold;
        case ConditionOp::Equals:    return std::fabs(value->floatValue - condition.threshold) <= kEpsilon;
        case ConditionOp::NotEquals: return std::fabs(value->floatValue - condition.threshold) > kEpsilon;
        case ConditionOp::IsTrue:    return value->boolValue;
        case ConditionOp::IsFalse:   return !value->boolValue;
        case ConditionOp::Triggered: return value->type == ParamType::Trigger && value->boolValue;
    }
    return false;
}

// Walks the graph for one instance. Owns the current state and the parameter
// values; the caller owns playback (see AnimationPlayer).
class StateMachineRuntime {
public:
    // `state` is non-null only on the frame the current state changes, which is
    // exactly when the caller has to start a crossfade.
    struct StepResult {
        const AnimationState*      state = nullptr;
        const AnimationTransition* transition = nullptr;   // null on the entry step
        float blendSeconds = 0.0f;

        bool changed() const { return state != nullptr; }
    };

    void setMachine(std::shared_ptr<const AnimationStateMachine> machine)
    {
        machine_ = std::move(machine);
        reset();
    }

    void clearMachine()
    {
        machine_.reset();
        currentState_.clear();
        entryPending_ = false;
        params_.clear();
    }

    const AnimationStateMachine* machine() const { return machine_.get(); }
    bool active() const { return machine_ != nullptr && !machine_->empty(); }

    void reset()
    {
        currentState_.clear();
        entryPending_ = false;
        if (!active()) return;

        params_.resetToDefaults(*machine_);
        if (const AnimationState* entry = machine_->entryState()) {
            currentState_ = entry->name;
            entryPending_ = true;
        }
    }

    ParameterBlock& parameters() { return params_; }
    const ParameterBlock& parameters() const { return params_; }

    const std::string& currentState() const { return currentState_; }

    const AnimationState* currentStateDef() const
    {
        return machine_ ? machine_->findState(currentState_) : nullptr;
    }

    // Jumps without evaluating conditions; used by tooling and by tests.
    bool setState(const std::string& stateName)
    {
        if (!machine_) return false;
        const AnimationState* state = machine_->findState(stateName);
        if (!state) return false;
        currentState_ = state->name;
        entryPending_ = true;
        return true;
    }

    // `normalizedStateTime` is playback progress of the current clip, 1.0 being
    // one full playthrough; it only matters for hasExitTime transitions.
    // At most one transition per call, so a chain of satisfied transitions
    // cannot burn through several states inside the same frame.
    StepResult step(float normalizedStateTime)
    {
        StepResult result;
        if (!active()) return result;

        if (entryPending_) {
            entryPending_ = false;
            result.state = machine_->findState(currentState_);
            result.blendSeconds = 0.0f;
            return result;
        }

        const AnimationTransition* chosen = nullptr;
        for (const AnimationTransition& transition : machine_->transitions) {
            if (!transition.fromAnyState() && transition.from != currentState_) continue;
            if (transition.to == currentState_) continue;
            if (transition.hasExitTime && normalizedStateTime < transition.exitTime) continue;
            if (!machine_->findState(transition.to)) continue;
            if (!conditionsMet(transition)) continue;
            chosen = &transition;
            break;
        }
        if (!chosen) return result;

        consumeTriggers(*chosen);
        currentState_ = chosen->to;
        result.state = machine_->findState(currentState_);
        result.transition = chosen;
        result.blendSeconds = chosen->blendSeconds;
        return result;
    }

private:
    bool conditionsMet(const AnimationTransition& transition) const
    {
        for (const AnimationCondition& condition : transition.conditions) {
            if (!evaluateCondition(condition, params_)) return false;
        }
        return true;
    }

    void consumeTriggers(const AnimationTransition& transition)
    {
        for (const AnimationCondition& condition : transition.conditions) {
            const ParameterBlock::Value* value = params_.find(condition.parameter);
            if (value && value->type == ParamType::Trigger) {
                params_.resetTrigger(condition.parameter);
            }
        }
    }

    std::shared_ptr<const AnimationStateMachine> machine_;
    ParameterBlock params_;
    std::string    currentState_;
    bool           entryPending_ = false;
};

} // namespace dash::anim
