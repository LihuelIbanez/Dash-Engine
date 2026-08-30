#include "StateMachinePanel.h"

#include "rendering/animation/AnimationClip.h"
#include "rendering/animation/AnimationFile.h"

#include "imgui.h"

#include <cstring>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;
namespace sm = dash::editor::animsm;

namespace {

const ImVec4 kErrorColor  (0.957f, 0.278f, 0.278f, 1.f);
const ImVec4 kWarningColor(0.902f, 0.596f, 0.212f, 1.f);
const ImVec4 kOkColor     (0.427f, 0.784f, 0.427f, 1.f);

constexpr int kMaxScannedFiles = 256;
constexpr const char* kAnyState = "<Any State>";

void copyToBuffer(char* dst, std::size_t size, const std::string& src)
{
    const std::size_t n = src.size() < size - 1 ? src.size() : size - 1;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

bool endsWith(const std::string& text, const std::string& suffix)
{
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// A double extension is invisible to fs::path::extension(), which only ever
// returns ".json" for foo.animsm.json.
std::vector<std::string> scanFor(const std::string& root, const std::string& suffix)
{
    std::vector<std::string> out;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return out;

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) return out;
    for (const fs::directory_entry& entry : it) {
        if (static_cast<int>(out.size()) >= kMaxScannedFiles) break;
        if (!entry.is_regular_file(ec)) continue;
        if (endsWith(entry.path().filename().string(), suffix)) out.push_back(entry.path().string());
    }
    return out;
}

const char* const kParamTypeLabels[] = {"Float", "Bool", "Trigger"};
const char* const kOpLabels[] = {"greater", "less", "equals", "notEquals",
                                 "isTrue", "isFalse", "triggered"};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Asset I/O
// ─────────────────────────────────────────────────────────────────────────────
bool StateMachinePanel::load(const std::string& path, std::string& outError)
{
    dash::anim::AnimationStateMachine loaded;
    if (!dash::anim::readStateMachine(path, loaded, outError)) return false;

    machine_ = std::move(loaded);
    path_ = path;
    dirty_ = false;
    selectedState_ = machine_.states.empty() ? -1 : 0;
    selectedParameter_ = machine_.parameters.empty() ? -1 : 0;
    selectedTransition_ = machine_.transitions.empty() ? -1 : 0;
    copyToBuffer(pathBuf_, sizeof(pathBuf_), path);
    copyToBuffer(nameBuf_, sizeof(nameBuf_), machine_.name);
    refreshIssues();
    resetSimulation();
    return true;
}

bool StateMachinePanel::save(const std::string& path, std::string& outError)
{
    if (path.empty()) {
        outError = "no destination path";
        return false;
    }
    std::error_code ec;
    const fs::path parent = fs::path(path).parent_path();
    if (!parent.empty()) fs::create_directories(parent, ec);

    if (!dash::anim::writeStateMachine(path, machine_, outError)) return false;
    path_ = path;
    dirty_ = false;
    scanned_ = false;
    return true;
}

bool StateMachinePanel::loadClipSource(const std::string& dashanimPath, std::string& outError)
{
    std::vector<dash::anim::AnimationClip> clips;
    if (!dash::anim::readAnimationClips(dashanimPath, clips, outError)) return false;

    clips_.clear();
    clips_.reserve(clips.size());
    for (const dash::anim::AnimationClip& clip : clips) clips_.push_back(clip.name);
    clipSourcePath_ = dashanimPath;
    copyToBuffer(clipPathBuf_, sizeof(clipPathBuf_), dashanimPath);
    refreshIssues();
    return true;
}

void StateMachinePanel::refreshIssues()
{
    issues_ = sm::validate(machine_, clips_);
}

void StateMachinePanel::newMachine()
{
    machine_ = dash::anim::AnimationStateMachine{};
    machine_.name = "controller";
    path_.clear();
    dirty_ = true;
    selectedState_ = selectedParameter_ = selectedTransition_ = -1;
    copyToBuffer(pathBuf_, sizeof(pathBuf_), std::string{});
    copyToBuffer(nameBuf_, sizeof(nameBuf_), machine_.name);
    refreshIssues();
    resetSimulation();
}

void StateMachinePanel::resetSimulation()
{
    simActive_ = false;
    sim_.clearMachine();
}

// ─────────────────────────────────────────────────────────────────────────────
void StateMachinePanel::draw(const std::string& assetsRoot, LogCallback logCb)
{
    ImGui::Begin("State Machine");

    drawSourceBar(assetsRoot, logCb);
    ImGui::Separator();

    if (machine_.states.empty() && path_.empty() && machine_.name.empty()) {
        ImGui::TextDisabled("Open a .animsm.json or press New to author a controller.");
        ImGui::End();
        return;
    }

    if (ImGui::InputText("Name", nameBuf_, sizeof(nameBuf_))) {
        machine_.name = nameBuf_;
        dirty_ = true;
    }

    // Initial state as a combo over the states that exist: typing it by hand is
    // the most common way to end up with a machine that starts nowhere.
    const std::vector<std::string> names = sm::stateNames(machine_);
    const char* initialPreview = machine_.initialState.empty() ? "<none>"
                                                               : machine_.initialState.c_str();
    if (ImGui::BeginCombo("Initial State", initialPreview)) {
        for (const std::string& name : names) {
            if (ImGui::Selectable(name.c_str(), name == machine_.initialState)) {
                machine_.initialState = name;
                dirty_ = true;
                refreshIssues();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginTabBar("##sm_tabs")) {
        if (ImGui::BeginTabItem("Parameters")) { drawParameters();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("States"))     { drawStates();      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Transitions")){ drawTransitions(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Simulation")) { drawSimulation();  ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    drawIssues();
    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
void StateMachinePanel::drawSourceBar(const std::string& assetsRoot, LogCallback& logCb)
{
    if (!scanned_) {
        scanned_ = true;
        foundMachines_  = scanFor(assetsRoot, dash::anim::kAnimStateMachineExtension);
        foundClipFiles_ = scanFor(assetsRoot, ".dashanim");
    }

    ImGui::SetNextItemWidth(340.f);
    ImGui::InputText("##sm_path", pathBuf_, sizeof(pathBuf_));
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string error;
        if (load(pathBuf_, error)) {
            status_ = "Loaded " + path_;
            statusIsError_ = false;
        } else {
            status_ = error;
            statusIsError_ = true;
        }
        if (logCb) logCb("[StateMachine] " + status_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string error;
        if (save(pathBuf_, error)) {
            status_ = "Saved " + path_;
            statusIsError_ = false;
        } else {
            status_ = error;
            statusIsError_ = true;
        }
        if (logCb) logCb("[StateMachine] " + status_);
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) newMachine();
    ImGui::SameLine();
    if (ImGui::Button("Rescan")) scanned_ = false;

    if (!foundMachines_.empty()) {
        ImGui::SetNextItemWidth(340.f);
        if (ImGui::BeginCombo("##sm_found", "Controllers in assets/")) {
            for (const std::string& file : foundMachines_) {
                if (!ImGui::Selectable(file.c_str())) continue;
                std::string error;
                if (load(file, error)) {
                    status_ = "Loaded " + file;
                    statusIsError_ = false;
                } else {
                    status_ = error;
                    statusIsError_ = true;
                }
                if (logCb) logCb("[StateMachine] " + status_);
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu found", foundMachines_.size());
    }

    // Clip source: states can only pick clips the model actually ships.
    ImGui::SetNextItemWidth(340.f);
    ImGui::InputText("##sm_clipsrc", clipPathBuf_, sizeof(clipPathBuf_));
    ImGui::SameLine();
    if (ImGui::Button("Load Clips")) {
        std::string error;
        if (loadClipSource(clipPathBuf_, error)) {
            status_ = "Loaded " + std::to_string(clips_.size()) + " clip(s)";
            statusIsError_ = false;
        } else {
            status_ = error;
            statusIsError_ = true;
        }
        if (logCb) logCb("[StateMachine] " + status_);
    }
    ImGui::SameLine();
    if (!foundClipFiles_.empty() && ImGui::BeginCombo("##sm_clipfiles", ".dashanim")) {
        for (const std::string& file : foundClipFiles_) {
            if (!ImGui::Selectable(file.c_str())) continue;
            std::string error;
            if (!loadClipSource(file, error)) {
                status_ = error;
                statusIsError_ = true;
                if (logCb) logCb("[StateMachine] " + status_);
            }
        }
        ImGui::EndCombo();
    }

    if (clips_.empty())
        ImGui::TextDisabled("No clip list loaded: clip names are not validated.");
    else
        ImGui::TextDisabled("%zu clip(s) from %s", clips_.size(),
                            fs::path(clipSourcePath_).filename().string().c_str());

    if (!status_.empty()) {
        ImGui::TextColored(statusIsError_ ? kErrorColor : kOkColor, "%s", status_.c_str());
    }
    if (dirty_) {
        ImGui::SameLine();
        ImGui::TextColored(kWarningColor, "unsaved changes");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void StateMachinePanel::drawParameters()
{
    if (ImGui::Button("Add Parameter")) {
        dash::anim::AnimationParameter param;
        param.name = sm::uniqueName(sm::parameterNames(machine_), "param");
        machine_.parameters.push_back(param);
        selectedParameter_ = static_cast<int>(machine_.parameters.size()) - 1;
        copyToBuffer(paramNameBuf_, sizeof(paramNameBuf_), param.name);
        dirty_ = true;
        refreshIssues();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedParameter_ < 0);
    if (ImGui::Button("Remove Parameter")) {
        sm::removeParameter(machine_, selectedParameter_);
        selectedParameter_ = machine_.parameters.empty() ? -1 : 0;
        dirty_ = true;
        refreshIssues();
    }
    ImGui::EndDisabled();

    ImGui::BeginChild("##sm_params", ImVec2(0.f, 190.f), ImGuiChildFlags_Borders);
    for (int i = 0; i < static_cast<int>(machine_.parameters.size()); ++i) {
        dash::anim::AnimationParameter& param = machine_.parameters[static_cast<std::size_t>(i)];
        ImGui::PushID(i);

        if (ImGui::Selectable("##sel", selectedParameter_ == i, 0, ImVec2(16.f, 0.f))) {
            selectedParameter_ = i;
            copyToBuffer(paramNameBuf_, sizeof(paramNameBuf_), param.name);
        }
        ImGui::SameLine();

        ImGui::SetNextItemWidth(150.f);
        if (selectedParameter_ == i) {
            if (ImGui::InputText("##name", paramNameBuf_, sizeof(paramNameBuf_),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                sm::renameParameter(machine_, i, paramNameBuf_);
                dirty_ = true;
                refreshIssues();
            }
        } else {
            ImGui::TextUnformatted(param.name.c_str());
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.f);
        int typeIdx = static_cast<int>(param.type);
        if (ImGui::Combo("##type", &typeIdx, kParamTypeLabels, IM_ARRAYSIZE(kParamTypeLabels))) {
            param.type = static_cast<dash::anim::ParamType>(typeIdx);
            dirty_ = true;
            refreshIssues();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.f);
        if (param.type == dash::anim::ParamType::Float) {
            if (ImGui::InputFloat("##def", &param.defaultFloat, 0.f, 0.f, "%.3f")) {
                dirty_ = true;
            }
        } else if (param.type == dash::anim::ParamType::Bool) {
            if (ImGui::Checkbox("default", &param.defaultBool)) dirty_ = true;
        } else {
            ImGui::TextDisabled("(set at runtime)");
        }

        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ─────────────────────────────────────────────────────────────────────────────
void StateMachinePanel::drawStates()
{
    if (ImGui::Button("Add State")) {
        dash::anim::AnimationState state;
        state.name = sm::uniqueName(sm::stateNames(machine_), "state");
        state.clip = clips_.empty() ? std::string{} : clips_.front();
        machine_.states.push_back(state);
        if (machine_.initialState.empty()) machine_.initialState = state.name;
        selectedState_ = static_cast<int>(machine_.states.size()) - 1;
        copyToBuffer(stateNameBuf_, sizeof(stateNameBuf_), state.name);
        dirty_ = true;
        refreshIssues();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedState_ < 0);
    if (ImGui::Button("Remove State")) {
        sm::removeState(machine_, selectedState_);
        selectedState_ = machine_.states.empty() ? -1 : 0;
        selectedTransition_ = machine_.transitions.empty() ? -1 : 0;
        dirty_ = true;
        refreshIssues();
    }
    ImGui::SameLine();
    if (ImGui::Button("Set Initial") && selectedState_ >= 0) {
        machine_.initialState = machine_.states[static_cast<std::size_t>(selectedState_)].name;
        dirty_ = true;
        refreshIssues();
    }
    ImGui::EndDisabled();

    ImGui::BeginChild("##sm_states", ImVec2(0.f, 190.f), ImGuiChildFlags_Borders);
    for (int i = 0; i < static_cast<int>(machine_.states.size()); ++i) {
        dash::anim::AnimationState& state = machine_.states[static_cast<std::size_t>(i)];
        const bool isInitial = state.name == machine_.initialState;
        ImGui::PushID(i);

        const std::string label = (isInitial ? "* " : "  ") + state.name;
        if (ImGui::Selectable(label.c_str(), selectedState_ == i)) {
            selectedState_ = i;
            copyToBuffer(stateNameBuf_, sizeof(stateNameBuf_), state.name);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (selectedState_ < 0 || selectedState_ >= static_cast<int>(machine_.states.size())) {
        ImGui::TextDisabled("Select a state to edit it.");
        return;
    }

    dash::anim::AnimationState& state = machine_.states[static_cast<std::size_t>(selectedState_)];
    ImGui::SeparatorText("State");

    if (ImGui::InputText("Name##state", stateNameBuf_, sizeof(stateNameBuf_),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        sm::renameState(machine_, selectedState_, stateNameBuf_);
        dirty_ = true;
        refreshIssues();
    }

    if (clips_.empty()) {
        char clipBuf[128];
        copyToBuffer(clipBuf, sizeof(clipBuf), state.clip);
        if (ImGui::InputText("Clip", clipBuf, sizeof(clipBuf))) {
            state.clip = clipBuf;
            dirty_ = true;
            refreshIssues();
        }
    } else if (ImGui::BeginCombo("Clip", state.clip.empty() ? "<none>" : state.clip.c_str())) {
        for (const std::string& clip : clips_) {
            if (!ImGui::Selectable(clip.c_str(), clip == state.clip)) continue;
            state.clip = clip;
            dirty_ = true;
            refreshIssues();
        }
        ImGui::EndCombo();
    }

    if (ImGui::DragFloat("Speed", &state.speed, 0.01f, -4.f, 4.f, "%.2f")) dirty_ = true;
    if (ImGui::Checkbox("Loop", &state.loop)) dirty_ = true;

    const std::vector<int> outgoing = sm::transitionsFrom(machine_, state.name);
    ImGui::TextDisabled("%zu outgoing transition(s)", outgoing.size());
    for (int index : outgoing) {
        const dash::anim::AnimationTransition& t =
            machine_.transitions[static_cast<std::size_t>(index)];
        ImGui::BulletText("-> %s  (%zu condition(s), blend %.2fs)", t.to.c_str(),
                          t.conditions.size(), static_cast<double>(t.blendSeconds));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void StateMachinePanel::drawTransitions()
{
    ImGui::BeginDisabled(machine_.states.empty());
    if (ImGui::Button("Add Transition")) {
        dash::anim::AnimationTransition transition;
        transition.from = machine_.states.front().name;
        transition.to = machine_.states.size() > 1 ? machine_.states[1].name
                                                   : machine_.states.front().name;
        machine_.transitions.push_back(transition);
        selectedTransition_ = static_cast<int>(machine_.transitions.size()) - 1;
        dirty_ = true;
        refreshIssues();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedTransition_ < 0);
    if (ImGui::Button("Remove Transition")) {
        machine_.transitions.erase(machine_.transitions.begin() + selectedTransition_);
        selectedTransition_ = machine_.transitions.empty() ? -1 : 0;
        dirty_ = true;
        refreshIssues();
    }
    ImGui::EndDisabled();

    ImGui::BeginChild("##sm_transitions", ImVec2(0.f, 160.f), ImGuiChildFlags_Borders);
    for (int i = 0; i < static_cast<int>(machine_.transitions.size()); ++i) {
        const dash::anim::AnimationTransition& t =
            machine_.transitions[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        const std::string label = (t.fromAnyState() ? std::string(kAnyState) : t.from) +
                                  "  ->  " + t.to;
        if (ImGui::Selectable(label.c_str(), selectedTransition_ == i)) selectedTransition_ = i;
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (selectedTransition_ < 0 ||
        selectedTransition_ >= static_cast<int>(machine_.transitions.size())) {
        ImGui::TextDisabled("Select a transition to edit it.");
        return;
    }

    dash::anim::AnimationTransition& transition =
        machine_.transitions[static_cast<std::size_t>(selectedTransition_)];
    ImGui::SeparatorText("Transition");

    if (ImGui::BeginCombo("From", transition.fromAnyState() ? kAnyState : transition.from.c_str())) {
        if (ImGui::Selectable(kAnyState, transition.fromAnyState())) {
            transition.from.clear();
            dirty_ = true;
            refreshIssues();
        }
        for (const dash::anim::AnimationState& state : machine_.states) {
            if (!ImGui::Selectable(state.name.c_str(), state.name == transition.from)) continue;
            transition.from = state.name;
            dirty_ = true;
            refreshIssues();
        }
        ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("To", transition.to.empty() ? "<none>" : transition.to.c_str())) {
        for (const dash::anim::AnimationState& state : machine_.states) {
            if (!ImGui::Selectable(state.name.c_str(), state.name == transition.to)) continue;
            transition.to = state.name;
            dirty_ = true;
            refreshIssues();
        }
        ImGui::EndCombo();
    }

    if (ImGui::DragFloat("Blend (s)", &transition.blendSeconds, 0.01f, 0.f, 4.f, "%.2f"))
        dirty_ = true;
    if (ImGui::Checkbox("Has Exit Time", &transition.hasExitTime)) dirty_ = true;
    if (transition.hasExitTime) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.f);
        if (ImGui::DragFloat("Exit Time", &transition.exitTime, 0.01f, 0.f, 8.f, "%.2f"))
            dirty_ = true;
    }

    ImGui::SeparatorText("Conditions");
    ImGui::BeginDisabled(machine_.parameters.empty());
    if (ImGui::Button("Add Condition")) {
        dash::anim::AnimationCondition condition;
        condition.parameter = machine_.parameters.front().name;
        transition.conditions.push_back(condition);
        dirty_ = true;
        refreshIssues();
    }
    ImGui::EndDisabled();
    if (machine_.parameters.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("declare a parameter first");
    }

    int removeCondition = -1;
    for (int i = 0; i < static_cast<int>(transition.conditions.size()); ++i) {
        dash::anim::AnimationCondition& condition =
            transition.conditions[static_cast<std::size_t>(i)];
        ImGui::PushID(i);

        ImGui::SetNextItemWidth(150.f);
        if (ImGui::BeginCombo("##param", condition.parameter.c_str())) {
            for (const dash::anim::AnimationParameter& param : machine_.parameters) {
                if (!ImGui::Selectable(param.name.c_str(), param.name == condition.parameter))
                    continue;
                condition.parameter = param.name;
                dirty_ = true;
                refreshIssues();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.f);
        int opIdx = static_cast<int>(condition.op);
        if (ImGui::Combo("##op", &opIdx, kOpLabels, IM_ARRAYSIZE(kOpLabels))) {
            condition.op = static_cast<dash::anim::ConditionOp>(opIdx);
            dirty_ = true;
            refreshIssues();
        }

        const bool needsThreshold = condition.op == dash::anim::ConditionOp::Greater ||
                                    condition.op == dash::anim::ConditionOp::Less ||
                                    condition.op == dash::anim::ConditionOp::Equals ||
                                    condition.op == dash::anim::ConditionOp::NotEquals;
        if (needsThreshold) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.f);
            if (ImGui::InputFloat("##value", &condition.threshold, 0.f, 0.f, "%.3f"))
                dirty_ = true;
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("x")) removeCondition = i;
        ImGui::PopID();
    }
    if (removeCondition >= 0) {
        transition.conditions.erase(transition.conditions.begin() + removeCondition);
        dirty_ = true;
        refreshIssues();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void StateMachinePanel::drawIssues()
{
    ImGui::SeparatorText("Problems");
    if (issues_.empty()) {
        ImGui::TextColored(kOkColor, "No problems found.");
        return;
    }

    ImGui::BeginChild("##sm_issues", ImVec2(0.f, 110.f), ImGuiChildFlags_Borders);
    for (const sm::Issue& issue : issues_) {
        const bool isError = issue.severity == sm::IssueSeverity::Error;
        ImGui::TextColored(isError ? kErrorColor : kWarningColor, "%s %s",
                           isError ? "[error]" : "[warn] ", issue.message.c_str());
    }
    ImGui::EndChild();
}

// ─────────────────────────────────────────────────────────────────────────────
// Simulation — the real StateMachineRuntime driven by hand-set parameters, so
// a graph can be proven before it is attached to an entity.
// ─────────────────────────────────────────────────────────────────────────────
void StateMachinePanel::drawSimulation()
{
    if (machine_.states.empty()) {
        ImGui::TextDisabled("Add a state first.");
        return;
    }

    if (ImGui::Button(simActive_ ? "Restart" : "Start")) {
        sim_.setMachine(std::make_shared<const dash::anim::AnimationStateMachine>(machine_));
        sim_.step(simNormalizedTime_);   // consume the entry step
        simActive_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) resetSimulation();
    ImGui::SameLine();
    ImGui::Checkbox("Auto step", &simAutoStep_);

    if (!simActive_) {
        ImGui::TextDisabled("Press Start to load the current graph into a runtime.");
        return;
    }

    ImGui::TextColored(kOkColor, "Current state: %s", sim_.currentState().c_str());
    if (const dash::anim::AnimationState* state = sim_.currentStateDef()) {
        ImGui::TextDisabled("clip '%s'  speed %.2f  loop %s", state->clip.c_str(),
                            static_cast<double>(state->speed), state->loop ? "yes" : "no");
    }

    ImGui::SetNextItemWidth(200.f);
    ImGui::SliderFloat("Normalized clip time", &simNormalizedTime_, 0.f, 2.f, "%.2f");

    ImGui::SeparatorText("Parameters");
    dash::anim::ParameterBlock& params = sim_.parameters();
    for (const dash::anim::AnimationParameter& param : machine_.parameters) {
        ImGui::PushID(param.name.c_str());
        if (param.type == dash::anim::ParamType::Float) {
            float value = params.getFloat(param.name);
            ImGui::SetNextItemWidth(180.f);
            if (ImGui::DragFloat(param.name.c_str(), &value, 0.01f, -100.f, 100.f, "%.2f"))
                params.setFloat(param.name, value);
        } else if (param.type == dash::anim::ParamType::Bool) {
            bool value = params.getBool(param.name);
            if (ImGui::Checkbox(param.name.c_str(), &value)) params.setBool(param.name, value);
        } else {
            if (ImGui::Button(("Fire " + param.name).c_str())) params.setTrigger(param.name);
            ImGui::SameLine();
            ImGui::TextDisabled(params.isTriggerSet(param.name) ? "pending" : "idle");
        }
        ImGui::PopID();
    }

    // One transition per step, the same rule the runtime follows in game.
    const bool step = ImGui::Button("Step") || simAutoStep_;
    if (step) {
        const dash::anim::StateMachineRuntime::StepResult result = sim_.step(simNormalizedTime_);
        if (result.changed() && !simAutoStep_)
            status_ = "Simulation moved to '" + sim_.currentState() + "'";
    }
}
