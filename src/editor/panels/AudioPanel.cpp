#include "AudioPanel.h"

#include "AddComponentCommand.h"
#include "AssetDatabase.h"
#include "AssetTypes.h"
#include "CommandStack.h"
#include "Reflection.h"
#include "SceneData.h"
#include "imgui.h"

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

const char* kBusNames[] = {"Master", "Sfx", "Music"};

AudioComponent* findAudio(EntityData& e)
{
    for (auto& comp : e.components)
        if (getVariantType(comp) == ComponentType::Audio)
            return &std::get<AudioComponent>(comp);
    return nullptr;
}

void previewButton(const char* label)
{
    ImGui::BeginDisabled();
    ImGui::Button(label, {90, 0});
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Preview is not wired to the AudioEngine yet.");
}

} // namespace

void AudioPanel::draw(const AssetDatabase& db,
                      SceneData& scene,
                      World& world,
                      CommandStack& commandStack,
                      uint64_t& selectedEntityId,
                      LogCallback logCb)
{
    ImGui::Begin("Audio");

    // ── Mixer ────────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Mixer");
    ImGui::SliderFloat("Master", &masterVolume_, 0.f, 1.f, "%.2f");
    ImGui::SliderFloat("SFX",    &sfxVolume_,    0.f, 1.f, "%.2f");
    ImGui::SliderFloat("Music",  &musicVolume_,  0.f, 1.f, "%.2f");
    ImGui::TextDisabled("Volumes are editor-only until the AudioEngine is connected.");

    // ── Audio assets ─────────────────────────────────────────────────────────
    ImGui::SeparatorText("Audio Assets");

    // Sorted so the list keeps a stable order across frames (records() is a hash map).
    std::map<std::string, const AssetRecord*> clips;
    for (const auto& [guid, rec] : db.records())
        if (rec.assetType == AssetType::Audio)
            clips.emplace(rec.sourcePath, &rec);

    if (clips.empty()) {
        ImGui::TextDisabled("No audio assets imported. Drop .wav/.mp3/.flac/.ogg into assets/.");
    } else if (ImGui::BeginTable("##audioassets", 3,
                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                   ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                   {0.f, 140.f})) {
        ImGui::TableSetupColumn("Source Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("GUID",        ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Preview",     ImGuiTableColumnFlags_WidthFixed, 100.f);
        ImGui::TableHeadersRow();

        for (const auto& [path, rec] : clips) {
            ImGui::TableNextRow();
            ImGui::PushID(rec->guid.c_str());

            ImGui::TableNextColumn();
            const bool selected = (selectedClipGuid_ == rec->guid);
            if (ImGui::Selectable(path.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                selectedClipGuid_ = rec->guid;

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", rec->guid.c_str());

            ImGui::TableNextColumn();
            previewButton("Play");

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // ── Emitters in the scene ────────────────────────────────────────────────
    ImGui::SeparatorText("Emitters");

    std::vector<EntityData*> emitters;
    for (auto& e : scene.entities)
        if (findAudio(e)) emitters.push_back(&e);

    if (emitters.empty()) {
        ImGui::TextDisabled("No entity in this scene has an AudioComponent.");
    } else if (ImGui::BeginTable("##emitters", 4,
                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                   ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                   {0.f, 140.f})) {
        ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Clip",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Bus",    ImGuiTableColumnFlags_WidthFixed, 60.f);
        ImGui::TableSetupColumn("Volume", ImGuiTableColumnFlags_WidthFixed, 60.f);
        ImGui::TableHeadersRow();

        for (EntityData* e : emitters) {
            const AudioComponent* audio = findAudio(*e);
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(e->id));

            ImGui::TableNextColumn();
            char label[160];
            std::snprintf(label, sizeof(label), "%s (#%llu)",
                          e->name.c_str(), static_cast<unsigned long long>(e->id));
            if (ImGui::Selectable(label, selectedEntityId == e->id,
                                  ImGuiSelectableFlags_SpanAllColumns))
                selectedEntityId = e->id;

            ImGui::TableNextColumn();
            if (audio->clip.empty())
                ImGui::TextDisabled("(none)");
            else
                ImGui::TextUnformatted(audio->clip.c_str());

            ImGui::TableNextColumn();
            const int bus = (audio->bus >= 0 && audio->bus < 3) ? audio->bus : 0;
            ImGui::TextUnformatted(kBusNames[bus]);

            ImGui::TableNextColumn();
            ImGui::Text("%.2f", static_cast<double>(audio->volume));

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // ── Add an emitter to the selected entity ────────────────────────────────
    ImGui::Separator();

    EntityData* target = nullptr;
    for (auto& e : scene.entities)
        if (e.id == selectedEntityId) { target = &e; break; }

    if (!target) {
        ImGui::TextDisabled("Select an entity to add an AudioComponent.");
    } else if (findAudio(*target)) {
        ImGui::TextDisabled("\"%s\" already has an AudioComponent.", target->name.c_str());
    } else {
        ImGui::Text("Selected: %s", target->name.c_str());
        ImGui::SameLine();
        if (ImGui::Button("+ Add AudioComponent")) {
            AudioComponent audio;
            audio.clip = selectedClipGuid_;  // empty when no asset is picked
            commandStack.execute(
                std::make_unique<AddComponentCommand>(target->id, ComponentVariant{audio}),
                scene, world);
            if (logCb)
                logCb("[Audio] AudioComponent added to \"" + target->name + "\".");
        }
    }

    ImGui::Separator();
    previewButton("Preview");
    ImGui::SameLine();
    ImGui::TextDisabled("AudioEngine playback pending.");

    ImGui::End();
}
