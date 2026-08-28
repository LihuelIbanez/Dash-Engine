#include "RuntimeInspectorPanel.h"

#include "EntityHierarchy.h"
#include "IconsFontAwesome6.h"
#include "imgui.h"

#include <cfloat>
#include <cstring>
#include <string>

namespace ri = dash::editor::runtimeinspect;

namespace {

const ImVec4 kChangedColor(0.902f, 0.596f, 0.212f, 1.f); // #E69836
const ImVec4 kAddedColor  (0.427f, 0.784f, 0.427f, 1.f); // #6DC86D
const ImVec4 kRemovedColor(0.957f, 0.278f, 0.278f, 1.f); // #F44747
const ImVec4 kPlayingColor(0.302f, 0.769f, 0.376f, 1.f); // #4DC460

const PropertyInfo* findProperty(ComponentType type, const std::string& field)
{
    for (const auto& p : getComponentMeta(type).properties)
        if (p.name == field) return &p;
    return nullptr;
}

std::string formatValue(const PropertyInfo& prop, const PropertyValue& value)
{
    if (prop.type == PropertyType::Enum) {
        const int idx = std::get<int>(value);
        if (idx >= 0 && idx < static_cast<int>(prop.enumValues.size()))
            return prop.enumValues[static_cast<std::size_t>(idx)];
    }
    return ri::valueToString(value);
}

std::string watchLabel(const SceneData& scene, const ri::FieldKey& key)
{
    const EntityData* e = dash::editor::findEntity(scene, key.entityId);
    const std::string owner = e ? e->name : ("#" + std::to_string(key.entityId));
    return owner + " . " + getComponentMeta(key.component).name + " . " + key.field;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
void RuntimeInspectorPanel::captureBaseline(const SceneData& scene)
{
    baseline_     = scene;
    haveBaseline_ = true;
}

void RuntimeInspectorPanel::clearBaseline()
{
    haveBaseline_ = false;
    baseline_     = SceneData{};
    diff_.clear();
}

void RuntimeInspectorPanel::clearWatches()
{
    watches_.clear();
    history_.clear();
}

bool RuntimeInspectorPanel::isWatched(const FieldKey& key) const
{
    for (const auto& w : watches_)
        if (w == key) return true;
    return false;
}

void RuntimeInspectorPanel::toggleWatch(const FieldKey& key)
{
    for (std::size_t i = 0; i < watches_.size(); ++i) {
        if (watches_[i] == key) {
            watches_.erase(watches_.begin() + static_cast<long>(i));
            history_.erase(key);
            return;
        }
    }
    watches_.push_back(key);
    history_.emplace(key, ri::ValueHistory(historySize_));
}

void RuntimeInspectorPanel::rebuildHistories(std::size_t capacity)
{
    historySize_ = capacity == 0 ? 1 : capacity;
    history_.clear();
    for (const auto& key : watches_)
        history_.emplace(key, ri::ValueHistory(historySize_));
}

// ─────────────────────────────────────────────────────────────────────────────
void RuntimeInspectorPanel::sampleWatches(const SceneData& scene, float dt)
{
    if (!recording_ || watches_.empty()) return;

    sampleAccum_ += dt;
    if (sampleAccum_ < sampleInterval_) return;
    sampleAccum_ = 0.f;

    for (const auto& key : watches_) {
        const EntityData* e = dash::editor::findEntity(scene, key.entityId);
        if (!e) continue;
        const ComponentVariant* comp = ri::findComponentOfType(*e, key.component);
        if (!comp) continue;
        const PropertyInfo* prop = findProperty(key.component, key.field);
        if (!prop) continue;

        float v = 0.f;
        if (!ri::asPlottable(ri::readProperty(*comp, *prop), v)) continue;

        auto it = history_.find(key);
        if (it == history_.end())
            it = history_.emplace(key, ri::ValueHistory(historySize_)).first;
        it->second.push(v);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void RuntimeInspectorPanel::drawToolbar(const SceneData& scene, bool isPlaying,
                                        const SceneData* baseline, LogCallback& logCb)
{
    ImGui::PushStyleColor(ImGuiCol_Text, isPlaying ? kPlayingColor : ImVec4(0.6f, 0.6f, 0.6f, 1.f));
    ImGui::TextUnformatted(isPlaying ? ICON_FA_PLAY "  PLAYING" : ICON_FA_STOPWATCH "  EDIT MODE");
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::TextDisabled("| %zu entities", scene.entities.size());

    ImGui::SameLine();
    if (!baseline) {
        ImGui::TextDisabled("| no baseline");
    } else if (diff_.empty()) {
        ImGui::TextDisabled("| no changes vs baseline");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, kChangedColor);
        ImGui::Text("| %zu changed  +%zu  -%zu",
                    diff_.changedFields.size(),
                    diff_.addedEntities.size(),
                    diff_.removedEntities.size());
        ImGui::PopStyleColor();
    }

    if (ImGui::Button(ICON_FA_CAMERA "  Capture baseline")) {
        captureBaseline(scene);
        if (logCb) logCb("[RuntimeInspector] Baseline captured (" +
                         std::to_string(scene.entities.size()) + " entities)");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Freeze the current scene state as the reference for the diff");

    ImGui::SameLine();
    ImGui::BeginDisabled(!haveBaseline_);
    if (ImGui::Button(ICON_FA_TRASH "  Clear baseline")) clearBaseline();
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(watches_.empty());
    if (ImGui::Button(ICON_FA_TRASH "  Clear watches")) clearWatches();
    ImGui::EndDisabled();

    ImGui::Checkbox("Only changed", &showChangedOnly_);
    ImGui::SameLine();
    ImGui::Checkbox("Record history", &recording_);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.f);
    int samples = static_cast<int>(historySize_);
    if (ImGui::SliderInt("Samples", &samples, 16, 512))
        rebuildHistories(static_cast<std::size_t>(samples));
}

// ─────────────────────────────────────────────────────────────────────────────
void RuntimeInspectorPanel::drawWatches(const SceneData& scene, uint64_t& selectedEntityId)
{
    ImGui::SeparatorText(("Watches (" + std::to_string(watches_.size()) + ")").c_str());

    if (watches_.empty()) {
        ImGui::TextDisabled("Click " ICON_FA_EYE " next to a field to pin it here.");
        return;
    }

    int removeIdx = -1;

    if (ImGui::BeginTable("##ri_watches", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Field",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Value",   ImGuiTableColumnFlags_WidthFixed, 130.f);
        ImGui::TableSetupColumn("History", ImGuiTableColumnFlags_WidthFixed, 170.f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(watches_.size()); ++i) {
            const FieldKey key = watches_[static_cast<std::size_t>(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow();

            // ── Col 0: label + remove ────────────────────────────────────────
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(ICON_FA_XMARK)) removeIdx = i;
            ImGui::SameLine();
            const bool changed = diff_.fieldChanged(key.entityId, key.component, key.field);
            if (changed) ImGui::PushStyleColor(ImGuiCol_Text, kChangedColor);
            ImGui::TextUnformatted(watchLabel(scene, key).c_str());
            if (changed) ImGui::PopStyleColor();
            if (ImGui::IsItemClicked()) selectedEntityId = key.entityId;

            // ── Col 1: current value ─────────────────────────────────────────
            ImGui::TableNextColumn();
            const EntityData*       e    = dash::editor::findEntity(scene, key.entityId);
            const ComponentVariant* comp = e ? ri::findComponentOfType(*e, key.component) : nullptr;
            const PropertyInfo*     prop = findProperty(key.component, key.field);
            if (comp && prop) {
                const PropertyValue now = ri::readProperty(*comp, *prop);
                if (changed) ImGui::PushStyleColor(ImGuiCol_Text, kChangedColor);
                ImGui::TextUnformatted(formatValue(*prop, now).c_str());
                if (changed) ImGui::PopStyleColor();
            } else {
                ImGui::TextDisabled("gone");
            }

            // ── Col 2: sparkline ─────────────────────────────────────────────
            ImGui::TableNextColumn();
            auto hit = history_.find(key);
            if (hit != history_.end() && !hit->second.empty()) {
                const std::vector<float> values = hit->second.ordered();
                float lo = 0.f, hi = 0.f;
                hit->second.minMax(lo, hi);
                if (hi - lo < 1e-6f) { lo -= 0.5f; hi += 0.5f; } // flat series still needs a range
                ImGui::PlotLines("##spark", values.data(), static_cast<int>(values.size()),
                                 0, nullptr, lo, hi, ImVec2(-FLT_MIN, 30.f));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("min %.4g   max %.4g   samples %zu",
                                      static_cast<double>(lo), static_cast<double>(hi),
                                      hit->second.size());
            } else {
                ImGui::TextDisabled("no samples");
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (removeIdx >= 0) {
        const FieldKey key = watches_[static_cast<std::size_t>(removeIdx)];
        watches_.erase(watches_.begin() + removeIdx);
        history_.erase(key);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void RuntimeInspectorPanel::drawComponentFields(const EntityData& entity,
                                                const ComponentVariant& comp)
{
    const ComponentType  type = getVariantType(comp);
    const ComponentMeta& meta = getComponentMeta(type);

    ImGui::PushID(static_cast<int>(type));

    const bool compChanged = [&] {
        for (const auto& p : meta.properties)
            if (diff_.fieldChanged(entity.id, type, p.name)) return true;
        return false;
    }();

    if (compChanged) ImGui::PushStyleColor(ImGuiCol_Text, kChangedColor);
    const bool open = ImGui::TreeNodeEx("##comp",
                                        ImGuiTreeNodeFlags_SpanAvailWidth |
                                        ImGuiTreeNodeFlags_DefaultOpen,
                                        "%s", meta.name.c_str());
    if (compChanged) ImGui::PopStyleColor();
    if (!open) { ImGui::PopID(); return; }

    if (ImGui::BeginTable("##ri_fields", 3, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("##watch", ImGuiTableColumnFlags_WidthFixed, 26.f);
        ImGui::TableSetupColumn("##name",  ImGuiTableColumnFlags_WidthFixed, 140.f);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

        for (const auto& prop : meta.properties) {
            const bool changed = diff_.fieldChanged(entity.id, type, prop.name);
            if (showChangedOnly_ && !changed) continue;

            FieldKey key;
            key.entityId  = entity.id;
            key.component = type;
            key.field     = prop.name;

            ImGui::PushID(prop.name.c_str());
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            const bool watched = isWatched(key);
            if (!watched) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.f));
            // Same ##id in both states so toggling does not move the widget.
            if (ImGui::SmallButton(watched ? ICON_FA_EYE "##watch" : ICON_FA_EYE_SLASH "##watch"))
                toggleWatch(key);
            if (!watched) ImGui::PopStyleColor();

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", prop.name.c_str());

            ImGui::TableNextColumn();
            const PropertyValue now = ri::readProperty(comp, prop);
            if (changed) ImGui::PushStyleColor(ImGuiCol_Text, kChangedColor);
            ImGui::TextUnformatted(formatValue(prop, now).c_str());
            if (changed) ImGui::PopStyleColor();

            if (changed && ImGui::IsItemHovered()) {
                for (const auto& ch : diff_.changedFields) {
                    if (!(ch.key == key)) continue;
                    ImGui::SetTooltip("%s  \xe2\x86\x92  %s",
                                      formatValue(prop, ch.before).c_str(),
                                      formatValue(prop, ch.after).c_str());
                    break;
                }
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::TreePop();
    ImGui::PopID();
}

// ─────────────────────────────────────────────────────────────────────────────
void RuntimeInspectorPanel::drawEntityNode(const SceneData& scene, uint64_t entityId,
                                           const std::unordered_set<uint64_t>& visible,
                                           uint64_t& selectedEntityId, int depth)
{
    if (depth > dash::editor::kMaxHierarchyDepth) return;
    if (visible.count(entityId) == 0) return;

    const EntityData* e = dash::editor::findEntity(scene, entityId);
    if (!e) return;

    const std::vector<uint64_t> kids = dash::editor::childrenOf(scene, entityId);

    ImGui::PushID(static_cast<int>(entityId & 0x7fffffffu));

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (kids.empty() && e->components.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (entityId == selectedEntityId) flags |= ImGuiTreeNodeFlags_Selected;

    const bool added   = diff_.entityAdded(entityId);
    const bool changed = diff_.entityChanged(entityId);
    if (added)        ImGui::PushStyleColor(ImGuiCol_Text, kAddedColor);
    else if (changed) ImGui::PushStyleColor(ImGuiCol_Text, kChangedColor);

    const bool open = ImGui::TreeNodeEx("##entity", flags, "%s  #%llu%s",
                                        e->name.c_str(),
                                        static_cast<unsigned long long>(entityId),
                                        added ? "   (spawned)" : "");
    if (added || changed) ImGui::PopStyleColor();

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        selectedEntityId = entityId;

    if (open) {
        const dash::editor::Transform3D world = dash::editor::worldTransform(scene, entityId);
        ImGui::TextDisabled("world  x %.2f   y %.2f   z %.2f",
                            static_cast<double>(world.x),
                            static_cast<double>(world.y),
                            static_cast<double>(world.z));

        for (const auto& comp : e->components) {
            if (!ri::componentMatchesFilter(*e, comp, filter_)) continue;
            drawComponentFields(*e, comp);
        }
        if (e->components.empty())
            ImGui::TextDisabled("(no components)");

        for (uint64_t kid : kids)
            drawEntityNode(scene, kid, visible, selectedEntityId, depth + 1);

        ImGui::TreePop();
    }

    ImGui::PopID();
}

// ─────────────────────────────────────────────────────────────────────────────
void RuntimeInspectorPanel::draw(const SceneData& scene,
                                 bool isPlaying,
                                 uint64_t& selectedEntityId,
                                 const SceneData* playSnapshot,
                                 LogCallback logCb)
{
    // Entering Play is what defines the "initial state" the diff refers to.
    if (isPlaying && !wasPlaying_ && !playSnapshot) captureBaseline(scene);
    wasPlaying_ = isPlaying;

    if (!ImGui::Begin("Runtime Inspector")) {
        ImGui::End();
        return;
    }

    // An explicit capture from the toolbar wins over the caller's snapshot.
    const SceneData* baseline = haveBaseline_ ? &baseline_ : playSnapshot;
    if (baseline) diff_ = ri::diffScenes(*baseline, scene);
    else          diff_.clear();

    drawToolbar(scene, isPlaying, baseline, logCb);
    ImGui::Separator();

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##ri_filter",
                             ICON_FA_MAGNIFYING_GLASS "  Filter by entity or component name...",
                             filterBuf_, sizeof(filterBuf_));
    filter_ = filterBuf_;

    sampleWatches(scene, ImGui::GetIO().DeltaTime);
    drawWatches(scene, selectedEntityId);

    ImGui::SeparatorText(ICON_FA_BINOCULARS "  Live state");

    std::unordered_set<uint64_t> visible;
    const bool filtering = !filter_.empty() || showChangedOnly_;
    for (const auto& e : scene.entities) {
        if (filtering) {
            if (!ri::entityMatchesFilter(e, filter_)) continue;
            if (showChangedOnly_ && !diff_.entityChanged(e.id) && !diff_.entityAdded(e.id))
                continue;
        }
        // Keep the ancestors so a matching child is still reachable in the tree.
        for (uint64_t id : dash::editor::ancestorChain(scene, e.id)) visible.insert(id);
    }

    ImGui::BeginChild("##ri_tree", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders);
    if (visible.empty()) {
        ImGui::TextDisabled("%s", scene.entities.empty()
                                      ? "Scene has no entities."
                                      : "Nothing matches the current filter.");
    } else {
        for (uint64_t rootId : dash::editor::rootEntities(scene))
            drawEntityNode(scene, rootId, visible, selectedEntityId, 0);
    }

    if (baseline && !diff_.removedEntities.empty()) {
        ImGui::SeparatorText("Despawned since baseline");
        for (uint64_t id : diff_.removedEntities) {
            const EntityData* gone = dash::editor::findEntity(*baseline, id);
            ImGui::PushStyleColor(ImGuiCol_Text, kRemovedColor);
            ImGui::Text("%s  #%llu", gone ? gone->name.c_str() : "?",
                        static_cast<unsigned long long>(id));
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::End();
}
