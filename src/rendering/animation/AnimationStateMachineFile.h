#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// .animsm.json — animation state machine (controller) asset
//
//   {
//     "version": 1,
//     "name": "wolf",
//     "initialState": "idle",
//     "parameters": [
//       { "name": "speed",  "type": "float",   "default": 0.0 },
//       { "name": "alive",  "type": "bool",    "default": true },
//       { "name": "attack", "type": "trigger" }
//     ],
//     "states": [
//       { "name": "idle", "clip": "Idle", "speed": 1.0, "loop": true }
//     ],
//     "transitions": [
//       { "from": "idle", "to": "walk", "blendSeconds": 0.2,
//         "conditions": [ { "parameter": "speed", "op": "greater", "value": 0.1 } ] },
//       { "from": "", "to": "attack", "blendSeconds": 0.1, "hasExitTime": false,
//         "conditions": [ { "parameter": "attack", "op": "triggered" } ] }
//     ]
//   }
//
// An empty or absent "from" means "any state". Ops: greater, less, equals,
// notEquals, isTrue, isFalse, triggered.
//
// Text rather than binary, the opposite call from .dashskel/.dashanim: a
// controller is a handful of small records that humans author and review, so
// git-diffability beats the size/parse win that dense keyframe arrays get from
// binary. The double extension follows the existing .mat.json convention —
// asset kind first, real container last, so generic JSON tooling still works.
// ─────────────────────────────────────────────────────────────────────────────

#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "rendering/animation/AnimationStateMachine.h"

namespace dash::anim {

inline constexpr uint32_t kAnimStateMachineVersion = 1;
inline constexpr const char* kAnimStateMachineExtension = ".animsm.json";

inline nlohmann::json toJson(const AnimationStateMachine& machine)
{
    nlohmann::json j;
    j["version"] = kAnimStateMachineVersion;
    j["name"] = machine.name;
    j["initialState"] = machine.initialState;

    nlohmann::json params = nlohmann::json::array();
    for (const AnimationParameter& param : machine.parameters) {
        nlohmann::json p;
        p["name"] = param.name;
        p["type"] = toString(param.type);
        if (param.type == ParamType::Bool)  p["default"] = param.defaultBool;
        if (param.type == ParamType::Float) p["default"] = param.defaultFloat;
        params.push_back(std::move(p));
    }
    j["parameters"] = std::move(params);

    nlohmann::json states = nlohmann::json::array();
    for (const AnimationState& state : machine.states) {
        nlohmann::json s;
        s["name"] = state.name;
        s["clip"] = state.clip;
        s["speed"] = state.speed;
        s["loop"] = state.loop;
        states.push_back(std::move(s));
    }
    j["states"] = std::move(states);

    nlohmann::json transitions = nlohmann::json::array();
    for (const AnimationTransition& transition : machine.transitions) {
        nlohmann::json t;
        t["from"] = transition.from;
        t["to"] = transition.to;
        t["blendSeconds"] = transition.blendSeconds;
        t["hasExitTime"] = transition.hasExitTime;
        t["exitTime"] = transition.exitTime;

        nlohmann::json conditions = nlohmann::json::array();
        for (const AnimationCondition& condition : transition.conditions) {
            nlohmann::json c;
            c["parameter"] = condition.parameter;
            c["op"] = toString(condition.op);
            c["value"] = condition.threshold;
            conditions.push_back(std::move(c));
        }
        t["conditions"] = std::move(conditions);
        transitions.push_back(std::move(t));
    }
    j["transitions"] = std::move(transitions);

    return j;
}

inline bool fromJson(const nlohmann::json& j, AnimationStateMachine& out, std::string& outError)
{
    if (!j.is_object()) {
        outError = "state machine root is not an object";
        return false;
    }

    const uint32_t version = j.value("version", kAnimStateMachineVersion);
    if (version > kAnimStateMachineVersion) {
        outError = "unsupported .animsm.json version " + std::to_string(version);
        return false;
    }

    AnimationStateMachine machine;
    machine.name = j.value("name", std::string{});
    machine.initialState = j.value("initialState", std::string{});

    if (j.contains("parameters") && j["parameters"].is_array()) {
        for (const nlohmann::json& p : j["parameters"]) {
            if (!p.is_object()) continue;
            AnimationParameter param;
            param.name = p.value("name", std::string{});
            if (param.name.empty()) {
                outError = "parameter without a name";
                return false;
            }
            const std::string typeText = p.value("type", std::string("float"));
            if (!parseParamType(typeText, param.type)) {
                outError = "unknown parameter type '" + typeText + "' on '" + param.name + "'";
                return false;
            }
            if (p.contains("default")) {
                if (param.type == ParamType::Bool) {
                    param.defaultBool = p["default"].is_boolean() ? p["default"].get<bool>() : false;
                } else if (param.type == ParamType::Float && p["default"].is_number()) {
                    param.defaultFloat = p["default"].get<float>();
                }
            }
            machine.parameters.push_back(std::move(param));
        }
    }

    if (j.contains("states") && j["states"].is_array()) {
        for (const nlohmann::json& s : j["states"]) {
            if (!s.is_object()) continue;
            AnimationState state;
            state.name = s.value("name", std::string{});
            if (state.name.empty()) {
                outError = "state without a name";
                return false;
            }
            state.clip = s.value("clip", std::string{});
            state.speed = s.value("speed", 1.0f);
            state.loop = s.value("loop", true);
            machine.states.push_back(std::move(state));
        }
    }

    if (j.contains("transitions") && j["transitions"].is_array()) {
        for (const nlohmann::json& t : j["transitions"]) {
            if (!t.is_object()) continue;
            AnimationTransition transition;
            transition.from = t.value("from", std::string{});
            transition.to = t.value("to", std::string{});
            if (transition.to.empty()) {
                outError = "transition without a destination state";
                return false;
            }
            transition.blendSeconds = t.value("blendSeconds", 0.2f);
            transition.hasExitTime = t.value("hasExitTime", false);
            transition.exitTime = t.value("exitTime", 1.0f);

            if (t.contains("conditions") && t["conditions"].is_array()) {
                for (const nlohmann::json& c : t["conditions"]) {
                    if (!c.is_object()) continue;
                    AnimationCondition condition;
                    condition.parameter = c.value("parameter", std::string{});
                    if (condition.parameter.empty()) {
                        outError = "condition without a parameter";
                        return false;
                    }
                    const std::string opText = c.value("op", std::string("greater"));
                    if (!parseConditionOp(opText, condition.op)) {
                        outError = "unknown condition op '" + opText + "'";
                        return false;
                    }
                    condition.threshold = c.value("value", 0.0f);
                    transition.conditions.push_back(std::move(condition));
                }
            }
            machine.transitions.push_back(std::move(transition));
        }
    }

    if (machine.states.empty()) {
        outError = "state machine has no states";
        return false;
    }

    out = std::move(machine);
    return true;
}

inline bool writeStateMachine(const std::string& path,
                              const AnimationStateMachine& machine,
                              std::string& outError)
{
    std::ofstream out(path);
    if (!out.is_open()) {
        outError = "cannot open for writing: " + path;
        return false;
    }
    out << toJson(machine).dump(2) << '\n';
    if (!out.good()) {
        outError = "write failed: " + path;
        return false;
    }
    return true;
}

inline bool readStateMachine(const std::string& path,
                             AnimationStateMachine& out,
                             std::string& outError)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        outError = "cannot open for reading: " + path;
        return false;
    }

    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        outError = "invalid JSON in " + path + ": " + e.what();
        return false;
    }
    return fromJson(j, out, outError);
}

} // namespace dash::anim
