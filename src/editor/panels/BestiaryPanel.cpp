#include "BestiaryPanel.h"

#include "imgui.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

void copyToBuffer(char* dst, std::size_t size, const std::string& src)
{
    const std::size_t n = src.size() < size - 1 ? src.size() : size - 1;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

std::string uniqueEnemyId(const GameplayDatabase& db, const std::string& base)
{
    auto exists = [&](const std::string& id) { return db.findEnemy(id) != nullptr; };
    if (!exists(base)) return base;
    for (int i = 2; i < 1000; ++i) {
        std::string candidate = base + "_" + std::to_string(i);
        if (!exists(candidate)) return candidate;
    }
    return base + "_new";
}

} // namespace

void BestiaryPanel::rescanPrefabs(const std::string& assetsRoot)
{
    prefabs_.clear();
    prefabsScannedRoot_ = assetsRoot;

    const fs::path dir = fs::path(assetsRoot) / "prefabs";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;

    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;

        std::ifstream in(entry.path());
        if (!in.is_open()) continue;
        json root = json::parse(in, nullptr, false);
        if (root.is_discarded() || !root.is_object()) continue;

        PrefabSummary s;
        s.path = entry.path().string();
        s.guid = root.value("guid", "");
        s.name = root.value("name", entry.path().stem().string());
        if (root.contains("components") && root["components"].is_array()) {
            s.componentCount = static_cast<int>(root["components"].size());
            for (const auto& c : root["components"]) {
                if (!s.componentSummary.empty()) s.componentSummary += ", ";
                s.componentSummary += c.value("type", "?");
            }
        }
        prefabs_.push_back(std::move(s));
    }

    std::sort(prefabs_.begin(), prefabs_.end(),
              [](const PrefabSummary& a, const PrefabSummary& b) { return a.name < b.name; });
}

void BestiaryPanel::draw(GameplayDatabase& db, const std::string& assetsRoot, const LogCallback& log)
{
    ImGui::Begin("Bestiary");

    if (ImGui::BeginTabBar("##bestiary_tabs")) {
        if (ImGui::BeginTabItem("Enemy Types")) {
            if (ImGui::Button("New Enemy")) {
                EnemyData d;
                d.id   = uniqueEnemyId(db, "new_enemy");
                d.name = "New Enemy";
                db.enemiesMutable().push_back(d);
                db.rebuildEnemyIndex();
                selected_ = static_cast<int>(db.enemies().size()) - 1;
                dirty_ = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate") && selected_ >= 0
                && selected_ < static_cast<int>(db.enemies().size())) {
                EnemyData copy = db.enemies()[static_cast<std::size_t>(selected_)];
                copy.id = uniqueEnemyId(db, copy.id);
                copy.name += " (Copy)";
                db.enemiesMutable().push_back(copy);
                db.rebuildEnemyIndex();
                selected_ = static_cast<int>(db.enemies().size()) - 1;
                dirty_ = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete") && selected_ >= 0
                && selected_ < static_cast<int>(db.enemies().size())) {
                db.enemiesMutable().erase(db.enemiesMutable().begin() + selected_);
                db.rebuildEnemyIndex();
                selected_ = -1;
                dirty_ = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!dirty_);
            if (ImGui::Button("Save")) {
                const std::string path = assetsRoot + "/gameplay/enemies.json";
                db.rebuildEnemyIndex();
                if (db.saveEnemiesToJson(path)) {
                    dirty_ = false;
                    if (log) log("Saved " + std::to_string(db.enemies().size()) + " enemy type(s) to " + path);
                } else if (log) {
                    log("[ERROR] Could not save " + path);
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu type(s)%s)", db.enemies().size(), dirty_ ? ", unsaved changes" : "");

            ImGui::Separator();
            ImGui::BeginChild("##enemy_list", ImVec2(300, 0), true);
            drawEnemyList(db);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("##enemy_inspector", ImVec2(0, 0), true);
            drawEnemyInspector(db, assetsRoot, log);
            ImGui::EndChild();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Prefab Library")) {
            if (prefabsScannedRoot_ != assetsRoot || ImGui::Button("Rescan")) {
                rescanPrefabs(assetsRoot);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu prefab(s) in %s/prefabs)", prefabs_.size(), assetsRoot.c_str());
            drawPrefabLibrary(assetsRoot);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void BestiaryPanel::drawEnemyList(GameplayDatabase& db)
{
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Filter...", filterBuf_, sizeof(filterBuf_));
    std::string filterLower(filterBuf_);
    for (auto& c : filterLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ImGui::BeginTable("##enemy_table", 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("HP", ImGuiTableColumnFlags_WidthFixed, 50.f);
        ImGui::TableHeadersRow();

        const auto& enemies = db.enemies();
        for (int i = 0; i < static_cast<int>(enemies.size()); ++i) {
            const EnemyData& d = enemies[static_cast<std::size_t>(i)];
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
            ImGui::Text("%d", d.maxHp);
        }
        ImGui::EndTable();
    }
}

void BestiaryPanel::drawEnemyInspector(GameplayDatabase& db, const std::string& /*assetsRoot*/, const LogCallback& /*log*/)
{
    if (selected_ < 0 || selected_ >= static_cast<int>(db.enemies().size())) {
        ImGui::TextDisabled("Select an enemy type on the left, or create a new one.");
        return;
    }

    EnemyData& d = db.enemiesMutable()[static_cast<std::size_t>(selected_)];

    char idBuf[64];
    char nameBuf[128];
    copyToBuffer(idBuf, sizeof(idBuf), d.id);
    copyToBuffer(nameBuf, sizeof(nameBuf), d.name);

    ImGui::SeparatorText(d.name.c_str());

    if (ImGui::InputText("Id", idBuf, sizeof(idBuf))) { d.id = idBuf; dirty_ = true; }
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) { d.name = nameBuf; dirty_ = true; }

    ImGui::SeparatorText("Vitals");
    if (ImGui::InputInt("Max HP", &d.maxHp)) { d.maxHp = std::max(1, d.maxHp); dirty_ = true; }
    if (ImGui::InputInt("Exp Reward", &d.expReward)) { d.expReward = std::max(0, d.expReward); dirty_ = true; }

    ImGui::SeparatorText("Combat stats");
    if (ImGui::InputInt("Attack", &d.attack)) dirty_ = true;
    if (ImGui::InputInt("Defense", &d.defense)) dirty_ = true;
    if (ImGui::InputInt("Magic Attack", &d.magicAttack)) dirty_ = true;
    if (ImGui::InputFloat("Speed", &d.speed)) dirty_ = true;
    if (ImGui::InputFloat("Crit Chance", &d.critChance, 0.01f)) dirty_ = true;
    if (ImGui::InputFloat("Attack Cooldown (s)", &d.attackCooldown, 0.05f)) dirty_ = true;

    ImGui::SeparatorText("AI");
    if (ImGui::InputFloat("Detection Radius", &d.detectionRadius)) dirty_ = true;
    if (ImGui::InputFloat("Attack Radius", &d.attackRadius)) dirty_ = true;
}

void BestiaryPanel::drawPrefabLibrary(const std::string& /*assetsRoot*/)
{
    if (ImGui::BeginTable("##prefab_table", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 160.f);
        ImGui::TableSetupColumn("Components", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthFixed, 160.f);
        ImGui::TableHeadersRow();

        for (const PrefabSummary& p : prefabs_) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", p.componentSummary.c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", p.guid.c_str());
        }
        ImGui::EndTable();
    }
}
