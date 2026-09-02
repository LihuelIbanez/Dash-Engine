#include "ClassesPanel.h"

#include "imgui.h"

#include <algorithm>
#include <cstring>

namespace {

void copyToBuffer(char* dst, std::size_t size, const std::string& src)
{
    const std::size_t n = src.size() < size - 1 ? src.size() : size - 1;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

std::string uniqueClassId(const GameplayDatabase& db, const std::string& base)
{
    auto exists = [&](const std::string& id) { return db.findPlayerClass(id) != nullptr; };
    if (!exists(base)) return base;
    for (int i = 2; i < 1000; ++i) {
        std::string candidate = base + "_" + std::to_string(i);
        if (!exists(candidate)) return candidate;
    }
    return base + "_new";
}

} // namespace

void ClassesPanel::draw(GameplayDatabase& db, const std::string& assetsRoot, const LogCallback& log)
{
    ImGui::Begin("Classes");

    if (ImGui::Button("New Class")) {
        PlayerClassData d;
        d.id   = uniqueClassId(db, "new_class");
        d.name = "New Class";
        db.playerClassesMutable().push_back(d);
        db.rebuildClassIndex();
        selected_ = static_cast<int>(db.playerClasses().size()) - 1;
        dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate") && selected_ >= 0
        && selected_ < static_cast<int>(db.playerClasses().size())) {
        PlayerClassData copy = db.playerClasses()[static_cast<std::size_t>(selected_)];
        copy.id = uniqueClassId(db, copy.id);
        copy.name += " (Copy)";
        db.playerClassesMutable().push_back(copy);
        db.rebuildClassIndex();
        selected_ = static_cast<int>(db.playerClasses().size()) - 1;
        dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && selected_ >= 0
        && selected_ < static_cast<int>(db.playerClasses().size())) {
        db.playerClassesMutable().erase(db.playerClassesMutable().begin() + selected_);
        db.rebuildClassIndex();
        selected_ = -1;
        dirty_ = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty_);
    if (ImGui::Button("Save")) {
        const std::string path = assetsRoot + "/gameplay/player_classes.json";
        db.rebuildClassIndex();
        if (db.savePlayerClassesToJson(path)) {
            dirty_ = false;
            if (log) log("Saved " + std::to_string(db.playerClasses().size()) + " class(es) to " + path);
        } else if (log) {
            log("[ERROR] Could not save " + path);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu class(es)%s)", db.playerClasses().size(), dirty_ ? ", unsaved changes" : "");

    ImGui::Separator();

    ImGui::BeginChild("##classes_list", ImVec2(300, 0), true);
    drawList(db);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##classes_inspector", ImVec2(0, 0), true);
    drawInspector(db, assetsRoot, log);
    ImGui::EndChild();

    ImGui::End();
}

void ClassesPanel::drawList(GameplayDatabase& db)
{
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Filter...", filterBuf_, sizeof(filterBuf_));
    std::string filterLower(filterBuf_);
    for (auto& c : filterLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ImGui::BeginTable("##classes_table", 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("HP/MP", ImGuiTableColumnFlags_WidthFixed, 70.f);
        ImGui::TableHeadersRow();

        const auto& classes = db.playerClasses();
        for (int i = 0; i < static_cast<int>(classes.size()); ++i) {
            const PlayerClassData& d = classes[static_cast<std::size_t>(i)];
            if (!filterLower.empty()) {
                std::string nameLower = d.name;
                for (auto& c : nameLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (nameLower.find(filterLower) == std::string::npos) continue;
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const bool isSelected = (selected_ == i);
            if (ImGui::Selectable(d.name.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                selected_ = i;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%d/%d", d.maxHp, d.maxMana);
        }
        ImGui::EndTable();
    }
}

void ClassesPanel::drawInspector(GameplayDatabase& db, const std::string& /*assetsRoot*/, const LogCallback& /*log*/)
{
    if (selected_ < 0 || selected_ >= static_cast<int>(db.playerClasses().size())) {
        ImGui::TextDisabled("Select a class on the left, or create a new one.");
        return;
    }

    PlayerClassData& d = db.playerClassesMutable()[static_cast<std::size_t>(selected_)];

    char idBuf[64];
    char nameBuf[128];
    char descBuf[256];
    copyToBuffer(idBuf, sizeof(idBuf), d.id);
    copyToBuffer(nameBuf, sizeof(nameBuf), d.name);
    copyToBuffer(descBuf, sizeof(descBuf), d.description);

    ImGui::SeparatorText(d.name.c_str());

    if (ImGui::InputText("Id", idBuf, sizeof(idBuf))) { d.id = idBuf; dirty_ = true; }
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) { d.name = nameBuf; dirty_ = true; }
    if (ImGui::InputTextMultiline("Description", descBuf, sizeof(descBuf), ImVec2(-1, 50))) {
        d.description = descBuf; dirty_ = true;
    }

    ImGui::SeparatorText("Vitals");
    if (ImGui::InputInt("Max HP", &d.maxHp)) { d.maxHp = std::max(1, d.maxHp); dirty_ = true; }
    if (ImGui::InputInt("Max Mana", &d.maxMana)) { d.maxMana = std::max(0, d.maxMana); dirty_ = true; }

    ImGui::SeparatorText("Combat stats");
    if (ImGui::InputInt("Attack", &d.attack)) dirty_ = true;
    if (ImGui::InputInt("Defense", &d.defense)) dirty_ = true;
    if (ImGui::InputInt("Magic Attack", &d.magicAttack)) dirty_ = true;
    if (ImGui::InputFloat("Speed", &d.speed)) dirty_ = true;
    if (ImGui::InputFloat("Crit Chance", &d.critChance, 0.01f)) dirty_ = true;
    if (ImGui::InputFloat("Attack Cooldown (s)", &d.attackCooldown, 0.05f)) dirty_ = true;
}
