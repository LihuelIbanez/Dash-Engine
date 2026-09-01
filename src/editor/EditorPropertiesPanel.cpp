// ═════════════════════════════════════════════════════════════════════════════
// EditorApp — Properties panel: entity header, generic component inspector
// (reflection-driven) and the Add Component workflow.
//
// Split out of EditorApp.cpp to keep that file navigable.
// ═════════════════════════════════════════════════════════════════════════════
#include "EditorApp.h"
#include "AddComponentCommand.h"
#include "EditComponentFieldCommand.h"
#include "EditPropertyCommand.h"
#include "PrefabAsset.h"
#include "RemoveComponentCommand.h"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

// ═════════════════════════════════════════════════════════════════════════════
// Properties Panel
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawPropertiesPanel()
{
    ImGui::Begin("Properties");

    // ── World settings (always visible) ──────────────────────────────────────
    if (ImGui::CollapsingHeader("World Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        int seed = static_cast<int>(scene_.worldSeed);
        if (ImGui::InputInt("Seed", &seed)) {
            scene_.worldSeed = static_cast<unsigned int>(seed);
            regenerateWorld();
            applySceneToWorld();
            scene_.modified = true;
        }

        char nameBuf[128];
        std::strncpy(nameBuf, scene_.sceneName.c_str(), sizeof(nameBuf));
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (ImGui::InputText("Scene Name", nameBuf, sizeof(nameBuf))) {
            scene_.sceneName = nameBuf;
            scene_.modified  = true;
        }

        ImGui::Separator();
        ImGui::TextDisabled("3D Isometric (Vulkan)");
        bool changed3D = false;

        auto applyCameraPreset = [&](float yaw, float pitch, float distance, float height, float zoom) {
            viewport3D_.isoYawDeg = yaw;
            viewport3D_.isoPitchDeg = pitch;
            viewport3D_.cameraDistance = distance;
            viewport3D_.cameraHeight = height;
            viewport3D_.zoom = zoom;
            changed3D = true;
        };

        ImGui::TextDisabled("Camera Presets");
        if (ImGui::Button("Diablo", ImVec2(90, 0))) {
            applyCameraPreset(45.0f, 35.264f, 10.0f, 2.5f, 1.0f);
        }
        ImGui::SameLine();
        if (ImGui::Button("RTS", ImVec2(90, 0))) {
            applyCameraPreset(45.0f, 42.0f, 16.0f, 4.0f, 0.85f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Close Follow", ImVec2(110, 0))) {
            applyCameraPreset(45.0f, 28.0f, 6.5f, 1.8f, 1.2f);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset", ImVec2(80, 0))) {
            applyCameraPreset(45.0f, 35.264f, 8.0f, 2.5f, 1.0f);
        }

        changed3D |= ImGui::SliderFloat("Iso Yaw", &viewport3D_.isoYawDeg, 30.0f, 60.0f, "%.1f deg");
        changed3D |= ImGui::SliderFloat("Iso Pitch", &viewport3D_.isoPitchDeg, 20.0f, 45.0f, "%.1f deg");
        changed3D |= ImGui::SliderFloat("Camera Distance", &viewport3D_.cameraDistance, 4.0f, 24.0f, "%.2f");
        changed3D |= ImGui::SliderFloat("Camera Height", &viewport3D_.cameraHeight, 0.0f, 12.0f, "%.2f");
        changed3D |= ImGui::SliderFloat("Viewport Zoom", &viewport3D_.zoom, 0.5f, 2.5f, "%.2f");
        changed3D |= ImGui::SliderFloat("Height Scale", &viewport3D_.heightScale, 12.0f, 72.0f, "%.1f px");
        changed3D |= ImGui::SliderFloat("Grid Opacity", &viewport3D_.gridOpacity, 0.0f, 0.8f, "%.2f");
        if (changed3D) {
            syncSceneRender3DSettingsFromUI();
            scene_.modified = true;
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Vulkan Viewport: Active");
    }

    ImGui::Separator();

    EntityData* ep = findEntityById(selectedEntityId_);
    if (!ep) {
        ImGui::TextDisabled("Select an entity to edit.");
        ImGui::End();
        return;
    }

    if (selection_.size() > 1) {
        ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f),
                           "%d entities selected - edits apply to all",
                           static_cast<int>(selection_.size()));
        ImGui::TextDisabled("Showing: %s (active)", ep->name.c_str());
        ImGui::Separator();
    }

    auto& e = *ep;

    // ── Entity header (EntityData-level fields) ───────────────────────────────
    if (ImGui::CollapsingHeader("Entity", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Prefab badge
        if (!e.prefabGuid.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.75f, 0.2f, 1.f));
            ImGui::Text("Prefab instance: %s", e.prefabGuid.c_str());
            ImGui::PopStyleColor();
            if (ImGui::Button("Reset All to Prefab Defaults")) {
                std::string prefabsDir = assetsRoot_ + "/prefabs";
                PrefabAsset prefab = findPrefabByGuid(prefabsDir, e.prefabGuid);
                if (!prefab.guid.empty()) {
                    e.components           = instantiate(prefab);
                    e.componentOverrides   = nlohmann::json::object();
                    scene_.modified        = true;
                }
            }
            ImGui::Separator();
        }

        // Name
        char nameBuf[128];
        std::strncpy(nameBuf, e.name.c_str(), sizeof(nameBuf));
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        static std::string nameSnap;
        ImGui::InputText("Name", nameBuf, sizeof(nameBuf));
        if (ImGui::IsItemActivated())            nameSnap = e.name;
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            std::string nv(nameBuf);
            if (nv != nameSnap)
                commandStack_.execute(std::make_unique<EditPropertyCommand>(
                    e.id, PropertyTarget::Name,
                    PropertyValue{nameSnap}, PropertyValue{nv}), scene_, world_);
        }

        ImGui::Text("Type: %s",
            e.type == EntityData::Type::Player ? "Player" : "Enemy");

        if (e.type == EntityData::Type::Player) {
            const char* classes[] = {"Warrior", "Mage", "Rogue", "Archer"};
            int cur = 0;
            for (int i = 0; i < 4; ++i)
                if (e.charClass == classes[i]) { cur = i; break; }
            if (ImGui::Combo("Class", &cur, classes, 4)) {
                std::string oldClass = e.charClass;
                std::string newClass = classes[cur];
                e.charClass = oldClass;
                commandStack_.execute(std::make_unique<EditPropertyCommand>(
                    e.id, PropertyTarget::CharClass,
                    PropertyValue{oldClass}, PropertyValue{newClass}), scene_, world_);
            }
        }
    }

    // ── Generic component inspector ───────────────────────────────────────────
    // Snapshot statics (only one field can be active at a time in ImGui)
    static PropertyValue fieldSnap;
    static bool          hasFieldSnap  = false;
    static char          strBuf[256]   = {};
    static std::string   strSnap;

    // Track which component type to remove (deferred to avoid iterator invalidation)
    ComponentType pendingRemove    = ComponentType::Transform;
    bool          hasPendingRemove = false;

    // Available sprite names from assets/sprites/*.png (without extension).
    std::vector<std::string> availableSprites;
    availableSprites.push_back("default");
    {
        std::error_code ec;
        fs::path spritesDir = fs::path(assetsRoot_) / "sprites";
        if (fs::exists(spritesDir, ec) && fs::is_directory(spritesDir, ec)) {
            for (const auto& entry : fs::directory_iterator(spritesDir, ec)) {
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() != ".png") continue;
                availableSprites.push_back(entry.path().stem().string());
            }
        }
    }
    std::sort(availableSprites.begin(), availableSprites.end());
    availableSprites.erase(std::unique(availableSprites.begin(), availableSprites.end()),
                           availableSprites.end());

    for (std::size_t ci = 0; ci < e.components.size(); ++ci) {
        auto& comp = e.components[ci];
        ComponentType ct = getVariantType(comp);
        const ComponentMeta& meta = getComponentMeta(ct);

        ImGui::PushID(static_cast<int>(ci));

        // Header with small remove button at the right edge
        bool sectionOpen = ImGui::CollapsingHeader(
            meta.name.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        float btnW = ImGui::GetFrameHeight();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - btnW);
        if (ImGui::SmallButton("x")) {
            pendingRemove    = ct;
            hasPendingRemove = true;
        }

        if (sectionOpen) {
            for (const auto& prop : meta.properties) {
                void* ptr = fieldPtr(comp, prop);
                ImGui::PushID(prop.name.c_str());

                switch (prop.type) {
                case PropertyType::Float: {
                    float* fptr = static_cast<float*>(ptr);
                    ImGui::DragFloat(prop.name.c_str(), fptr, 0.05f);
                    if (ImGui::IsItemActivated()) {
                        fieldSnap    = *fptr;
                        hasFieldSnap = true;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit() && hasFieldSnap) {
                        float nv = *fptr;
                        if (nv != std::get<float>(fieldSnap)) {
                            *fptr = std::get<float>(fieldSnap);
                            applyComponentFieldEdit(e.id, ct, prop,
                                                    fieldSnap, PropertyValue{nv});
                        }
                        hasFieldSnap = false;
                    }
                    break;
                }
                case PropertyType::Int: {
                    int* iptr = static_cast<int*>(ptr);
                    ImGui::DragInt(prop.name.c_str(), iptr);
                    if (ImGui::IsItemActivated()) {
                        fieldSnap    = *iptr;
                        hasFieldSnap = true;
                    }
                    if (ImGui::IsItemDeactivatedAfterEdit() && hasFieldSnap) {
                        int nv = *iptr;
                        if (nv != std::get<int>(fieldSnap)) {
                            *iptr = std::get<int>(fieldSnap);
                            applyComponentFieldEdit(e.id, ct, prop,
                                                    fieldSnap, PropertyValue{nv});
                        }
                        hasFieldSnap = false;
                    }
                    break;
                }
                case PropertyType::String: {
                    std::string* sptr = static_cast<std::string*>(ptr);
                    bool handledWithSpritePicker = false;

                    // Render.sprite: pick from discovered sprites for faster workflows.
                    if (ct == ComponentType::Render && prop.name == "sprite") {
                        handledWithSpritePicker = true;

                        std::vector<std::string> pickerItems = availableSprites;
                        if (std::find(pickerItems.begin(), pickerItems.end(), *sptr) == pickerItems.end())
                            pickerItems.insert(pickerItems.begin(), *sptr);

                        std::string comboLabel = prop.name + "##picker";
                        if (ImGui::BeginCombo(comboLabel.c_str(), sptr->c_str())) {
                            for (const auto& item : pickerItems) {
                                bool selected = (*sptr == item);
                                if (ImGui::Selectable(item.c_str(), selected) && item != *sptr) {
                                    applyComponentFieldEdit(e.id, ct, prop,
                                                            PropertyValue{*sptr}, PropertyValue{item});
                                }
                                if (selected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }

                        std::string manualLabel = "Manual##" + prop.name;
                        std::strncpy(strBuf, sptr->c_str(), 255);
                        strBuf[255] = '\0';
                        ImGui::InputText(manualLabel.c_str(), strBuf, sizeof(strBuf));
                        if (ImGui::IsItemActivated())
                            strSnap = *sptr;
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            std::string nv(strBuf);
                            if (nv != strSnap)
                                applyComponentFieldEdit(e.id, ct, prop,
                                                        PropertyValue{strSnap}, PropertyValue{nv});
                        }
                    }

                    if (!handledWithSpritePicker) {
                        std::strncpy(strBuf, sptr->c_str(), 255);
                        strBuf[255] = '\0';
                        ImGui::InputText(prop.name.c_str(), strBuf, sizeof(strBuf));
                        if (ImGui::IsItemActivated())
                            strSnap = *sptr;
                        if (ImGui::IsItemDeactivatedAfterEdit()) {
                            std::string nv(strBuf);
                            if (nv != strSnap)
                                applyComponentFieldEdit(e.id, ct, prop,
                                                        PropertyValue{strSnap}, PropertyValue{nv});
                        }
                    }
                    break;
                }
                case PropertyType::Bool: {
                    bool* bptr  = static_cast<bool*>(ptr);
                    bool  prev  = *bptr;
                    if (ImGui::Checkbox(prop.name.c_str(), bptr) && *bptr != prev) {
                        bool nv = *bptr;
                        *bptr   = prev;
                        applyComponentFieldEdit(e.id, ct, prop,
                                                PropertyValue{prev}, PropertyValue{nv});
                    }
                    break;
                }
                case PropertyType::Enum: {
                    int* iptr = static_cast<int*>(ptr);
                    int  prev = *iptr;
                    std::vector<const char*> items;
                    for (const auto& s : prop.enumValues) items.push_back(s.c_str());
                    if (ImGui::Combo(prop.name.c_str(), iptr,
                                     items.data(), static_cast<int>(items.size()))) {
                        int nv = *iptr;
                        *iptr  = prev;
                        applyComponentFieldEdit(e.id, ct, prop,
                                                PropertyValue{prev}, PropertyValue{nv});
                    }
                    break;
                }
                } // switch

                ImGui::PopID();
            } // for props
        } // if sectionOpen

        ImGui::PopID();

        if (hasPendingRemove) break; // stop iterating; will remove after loop
    }

    // Apply deferred component removal
    if (hasPendingRemove) {
        for (auto& comp : e.components) {
            if (getVariantType(comp) == pendingRemove) {
                commandStack_.execute(
                    std::make_unique<RemoveComponentCommand>(e.id, comp),
                    scene_, world_);
                break;
            }
        }
    }

    // Recompute overrides for prefab instances after any change
    if (!e.prefabGuid.empty()) {
        std::string prefabsDir = assetsRoot_ + "/prefabs";
        PrefabAsset prefab = findPrefabByGuid(prefabsDir, e.prefabGuid);
        if (!prefab.guid.empty())
            e.componentOverrides = computeOverrides(prefab, e.components);
    }

    // ── Add Component button ──────────────────────────────────────────────────
    ImGui::Separator();

    // Collect component types not yet present on this entity
    static int addSel = 0;
    std::vector<ComponentType> missing;
    for (int i = 0; i <= static_cast<int>(ComponentType::Physics); ++i) {
        ComponentType ct = static_cast<ComponentType>(i);
        bool found = false;
        for (const auto& comp : e.components)
            if (getVariantType(comp) == ct) { found = true; break; }
        if (!found) missing.push_back(ct);
    }

    if (!missing.empty()) {
        if (addSel >= static_cast<int>(missing.size())) addSel = 0;
        std::vector<const char*> names;
        for (auto ct : missing) names.push_back(getComponentMeta(ct).name.c_str());
        ImGui::SetNextItemWidth(180.f);
        ImGui::Combo("##addcomp", &addSel, names.data(), static_cast<int>(names.size()));
        ImGui::SameLine();
        if (ImGui::Button("+ Add")) {
            ComponentType toAdd = missing[addSel];
            ComponentVariant newComp;
            switch (toAdd) {
            case ComponentType::Transform: newComp = TransformComponent{}; break;
            case ComponentType::Render:    newComp = RenderComponent{};    break;
            case ComponentType::Health:    newComp = HealthComponent{};    break;
            case ComponentType::Mana:      newComp = ManaComponent{};      break;
            case ComponentType::Stats:     newComp = StatsComponent{};     break;
            case ComponentType::Combat:    newComp = CombatComponent{};    break;
            case ComponentType::AI:        newComp = AIComponent{};        break;
            case ComponentType::Physics:   newComp = PhysicsComponent{};   break;
            default: break; // Light/Animation/Audio: added via their own UI, never in `missing`
            }
            commandStack_.execute(
                std::make_unique<AddComponentCommand>(e.id, newComp),
                scene_, world_);
        }
    } else {
        ImGui::TextDisabled("All components present.");
    }

    ImGui::End();
}
