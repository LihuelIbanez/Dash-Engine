#include "AnimationPanel.h"

#include "Reflection.h"
#include "SceneData.h"
#include "rendering/animation/AnimationWiring.h"

#include "imgui.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

const ImVec4 kOkColor   (0.427f, 0.784f, 0.427f, 1.f);
const ImVec4 kWarnColor (0.902f, 0.596f, 0.212f, 1.f);

AnimationComponent* findAnimation(EntityData& e)
{
    for (ComponentVariant& comp : e.components)
        if (getVariantType(comp) == ComponentType::Animation)
            return &std::get<AnimationComponent>(comp);
    return nullptr;
}

const RenderComponent* findRender(const EntityData& e)
{
    for (const ComponentVariant& comp : e.components)
        if (getVariantType(comp) == ComponentType::Render)
            return &std::get<RenderComponent>(comp);
    return nullptr;
}

} // namespace

void AnimationPanel::draw(SceneData& scene,
                          uint64_t selectedEntityId,
                          dash::anim::AnimationSetCache& sets,
                          PlayerMap& players,
                          const MeshPathResolver& resolveMeshPath,
                          LogCallback logCb)
{
    ImGui::Begin("Animation");

    // ── Target entity ────────────────────────────────────────────────────────
    std::vector<EntityData*> animated;
    for (EntityData& e : scene.entities)
        if (findAnimation(e)) animated.push_back(&e);

    if (animated.empty()) {
        ImGui::TextDisabled("No entity in the scene carries an AnimationComponent.");
        ImGui::TextDisabled("Add one from the Properties panel to preview its clips.");
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Follow selection", &followSelection_);
    if (followSelection_ && selectedEntityId != 0) {
        const bool selectedIsAnimated = std::any_of(
            animated.begin(), animated.end(),
            [&](const EntityData* e) { return e->id == selectedEntityId; });
        if (selectedIsAnimated) target_ = selectedEntityId;
    }

    const bool targetPresent = std::any_of(animated.begin(), animated.end(),
                                           [&](const EntityData* e) { return e->id == target_; });
    if (!targetPresent) target_ = animated.front()->id;

    EntityData* entity = *std::find_if(animated.begin(), animated.end(),
                                       [&](EntityData* e) { return e->id == target_; });

    ImGui::SetNextItemWidth(260.f);
    if (ImGui::BeginCombo("Entity", entity->name.c_str())) {
        for (EntityData* candidate : animated) {
            const std::string label = candidate->name + " #" + std::to_string(candidate->id);
            if (ImGui::Selectable(label.c_str(), candidate->id == target_)) target_ = candidate->id;
        }
        ImGui::EndCombo();
    }

    AnimationComponent* anim = findAnimation(*entity);
    if (!anim) {           // the combo can outlive a component removed this frame
        ImGui::End();
        return;
    }

    // ── Model, clips and live player ─────────────────────────────────────────
    const RenderComponent* render = findRender(*entity);
    const std::string meshPath =
        (render && resolveMeshPath) ? resolveMeshPath(render->mesh) : std::string{};

    const dash::anim::AnimationSet* set = nullptr;
    if (!meshPath.empty()) {
        const dash::anim::AnimationSet& loaded = sets.load(meshPath);
        if (loaded.valid()) set = &loaded;
    }

    auto playerIt = players.find(entity->id);
    dash::anim::AnimationPlayer* player = playerIt == players.end() ? nullptr : &playerIt->second;

    ImGui::Separator();
    if (!set) {
        ImGui::TextColored(kWarnColor, "No skeleton for mesh '%s'.",
                           render ? render->mesh.c_str() : "<no RenderComponent>");
        ImGui::TextDisabled("A .dashskel/.dashanim pair next to the .dashmesh is required.");
    } else {
        ImGui::Text("Model: %s", meshPath.c_str());
        ImGui::Text("Bones: %zu    Clips: %zu",
                    set->skeleton->boneCount(), set->clips.size());
    }
    ImGui::TextColored(player ? kOkColor : kWarnColor, "%s",
                       player ? "Live viewport player attached."
                              : "No live player yet: the entity is not drawn in the viewport.");

    // ── Clip picker ──────────────────────────────────────────────────────────
    ImGui::SeparatorText("Clips");
    const dash::anim::AnimationClip* currentClip = nullptr;
    if (set) {
        for (const auto& clip : set->clips) {
            if (!clip) continue;
            if (clip->name == anim->clip) currentClip = clip.get();

            ImGui::PushID(clip->name.c_str());
            if (ImGui::Selectable(clip->name.c_str(), clip->name == anim->clip)) {
                anim->clip = clip->name;
                anim->playing = true;
                if (logCb) logCb("[Animation] " + entity->name + " -> clip '" + clip->name + "'");
            }
            ImGui::SameLine(320.f);
            ImGui::TextDisabled("%.2fs", static_cast<double>(clip->durationSeconds()));
            ImGui::PopID();
        }
        if (set->clips.empty()) ImGui::TextDisabled("The model ships no clips.");
    } else {
        char clipBuf[128] = {0};
        const std::size_t n = std::min(anim->clip.size(), sizeof(clipBuf) - 1);
        std::copy_n(anim->clip.begin(), n, clipBuf);
        if (ImGui::InputText("Clip", clipBuf, sizeof(clipBuf))) anim->clip = clipBuf;
    }

    // ── Transport ────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Transport");
    if (ImGui::Button("Play", {70, 0})) {
        anim->playing = true;
        if (player) player->setPaused(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Pause", {70, 0})) {
        anim->playing = false;
        if (player) player->setPaused(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop", {70, 0})) {
        anim->playing = false;
        if (player) dash::editor::animpanel::scrubTo(*player, anim->clip, 0.0f);
    }
    ImGui::SameLine();
    ImGui::TextDisabled(anim->playing ? "playing" : "paused");

    ImGui::SetNextItemWidth(200.f);
    ImGui::DragFloat("Speed", &anim->speed, 0.01f, -4.f, 4.f, "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("1x")) anim->speed = 1.0f;
    ImGui::Checkbox("Loop", &anim->loop);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.f);
    ImGui::DragFloat("Blend (s)", &anim->blendSeconds, 0.01f, 0.f, 2.f, "%.2f");

    // ── Scrub ────────────────────────────────────────────────────────────────
    const float duration = currentClip ? currentClip->durationSeconds() : 0.0f;
    float time = player ? player->currentTimeSeconds() : 0.0f;

    ImGui::BeginDisabled(!player || duration <= 0.0f);
    ImGui::SetNextItemWidth(-140.f);
    if (ImGui::SliderFloat("Time", &time, 0.0f, duration > 0.0f ? duration : 1.0f, "%.3f s")) {
        anim->playing = false;         // scrubbing implies taking manual control
        if (player) {
            player->setPaused(true);
            dash::editor::animpanel::scrubTo(*player, anim->clip, time);
        }
    }
    ImGui::EndDisabled();

    ImGui::Text("Clip: %s", anim->clip.empty() ? "<bind pose>" : anim->clip.c_str());
    ImGui::Text("Time: %.3f s / %.3f s", static_cast<double>(time), static_cast<double>(duration));
    if (player) {
        ImGui::Text("Bones in palette: %zu    Blending: %s",
                    player->boneMatrices().size(), player->isBlending() ? "yes" : "no");
    }

    if (!anim->stateMachine.empty())
        ImGui::TextDisabled("Controller: %s", anim->stateMachine.c_str());

    ImGui::End();
}
