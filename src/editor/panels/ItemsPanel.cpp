#include "ItemsPanel.h"

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

std::string uniqueItemId(const GameplayDatabase& db, const std::string& base)
{
    auto exists = [&](const std::string& id) { return db.findItem(id) != nullptr; };
    if (!exists(base)) return base;
    for (int i = 2; i < 1000; ++i) {
        std::string candidate = base + "_" + std::to_string(i);
        if (!exists(candidate)) return candidate;
    }
    return base + "_new";
}

constexpr const char* kTypeNames[]   = { "Weapon", "Armor", "Consumable", "Material", "Quest", "Misc" };
constexpr const char* kRarityNames[] = { "Normal", "Magic", "Rare", "Legendary", "Unique" };

const ImVec4 kRarityColors[] = {
    { 0.75f, 0.75f, 0.75f, 1.f },  // Normal
    { 0.35f, 0.55f, 1.00f, 1.f },  // Magic
    { 1.00f, 0.85f, 0.20f, 1.f },  // Rare
    { 1.00f, 0.55f, 0.15f, 1.f },  // Legendary
    { 0.75f, 0.30f, 0.90f, 1.f },  // Unique
};

} // namespace

void ItemsPanel::draw(GameplayDatabase& db, const std::string& assetsRoot, const LogCallback& log)
{
    ImGui::Begin("Items");

    if (ImGui::Button("New Item")) {
        ItemData d;
        d.id   = uniqueItemId(db, "new_item");
        d.name = "New Item";
        db.itemsMutable().push_back(d);
        db.rebuildItemIndex();
        selected_ = static_cast<int>(db.items().size()) - 1;
        dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate") && selected_ >= 0
        && selected_ < static_cast<int>(db.items().size())) {
        ItemData copy = db.items()[static_cast<std::size_t>(selected_)];
        copy.id = uniqueItemId(db, copy.id);
        copy.name += " (Copy)";
        db.itemsMutable().push_back(copy);
        db.rebuildItemIndex();
        selected_ = static_cast<int>(db.items().size()) - 1;
        dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && selected_ >= 0
        && selected_ < static_cast<int>(db.items().size())) {
        db.itemsMutable().erase(db.itemsMutable().begin() + selected_);
        db.rebuildItemIndex();
        selected_ = -1;
        dirty_ = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty_);
    if (ImGui::Button("Save")) {
        const std::string path = assetsRoot + "/gameplay/items.json";
        db.rebuildItemIndex();
        if (db.saveItemsToJson(path)) {
            dirty_ = false;
            if (log) log("Saved " + std::to_string(db.items().size()) + " item(s) to " + path);
        } else if (log) {
            log("[ERROR] Could not save " + path);
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu item(s)%s)", db.items().size(), dirty_ ? ", unsaved changes" : "");

    ImGui::Separator();

    ImGui::BeginChild("##items_list", ImVec2(320, 0), true);
    drawList(db);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##items_inspector", ImVec2(0, 0), true);
    drawInspector(db, assetsRoot, log);
    ImGui::EndChild();

    ImGui::End();
}

void ItemsPanel::drawList(GameplayDatabase& db)
{
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Filter...", filterBuf_, sizeof(filterBuf_));
    std::string filterLower(filterBuf_);
    for (auto& c : filterLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ImGui::BeginTable("##items_table", 2,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Rarity", ImGuiTableColumnFlags_WidthFixed, 80.f);
        ImGui::TableHeadersRow();

        const auto& items = db.items();
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const ItemData& d = items[static_cast<std::size_t>(i)];
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
            const int rarityIdx = static_cast<int>(d.rarity);
            ImGui::TextColored(kRarityColors[rarityIdx], "%s", kRarityNames[rarityIdx]);
        }
        ImGui::EndTable();
    }
}

void ItemsPanel::drawInspector(GameplayDatabase& db, const std::string& /*assetsRoot*/, const LogCallback& /*log*/)
{
    if (selected_ < 0 || selected_ >= static_cast<int>(db.items().size())) {
        ImGui::TextDisabled("Select an item on the left, or create a new one.");
        return;
    }

    ItemData& d = db.itemsMutable()[static_cast<std::size_t>(selected_)];

    char idBuf[64];
    char nameBuf[128];
    char descBuf[256];
    char iconBuf[128];
    char effectBuf[64];
    copyToBuffer(idBuf, sizeof(idBuf), d.id);
    copyToBuffer(nameBuf, sizeof(nameBuf), d.name);
    copyToBuffer(descBuf, sizeof(descBuf), d.description);
    copyToBuffer(iconBuf, sizeof(iconBuf), d.icon);
    copyToBuffer(effectBuf, sizeof(effectBuf), d.consumableEffect);

    ImGui::SeparatorText(d.name.c_str());

    if (ImGui::InputText("Id", idBuf, sizeof(idBuf))) { d.id = idBuf; dirty_ = true; }
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) { d.name = nameBuf; dirty_ = true; }
    if (ImGui::InputTextMultiline("Description", descBuf, sizeof(descBuf), ImVec2(-1, 50))) {
        d.description = descBuf; dirty_ = true;
    }
    if (ImGui::InputText("Icon", iconBuf, sizeof(iconBuf))) { d.icon = iconBuf; dirty_ = true; }

    int typeIdx = static_cast<int>(d.type);
    if (ImGui::Combo("Type", &typeIdx, kTypeNames, IM_ARRAYSIZE(kTypeNames))) {
        d.type = static_cast<ItemType>(typeIdx); dirty_ = true;
    }
    int rarityIdx = static_cast<int>(d.rarity);
    if (ImGui::Combo("Rarity", &rarityIdx, kRarityNames, IM_ARRAYSIZE(kRarityNames))) {
        d.rarity = static_cast<ItemRarity>(rarityIdx); dirty_ = true;
    }

    if (ImGui::InputInt("Level Req.", &d.levelReq)) { d.levelReq = std::max(1, d.levelReq); dirty_ = true; }
    if (ImGui::InputInt("Gold Value", &d.goldValue)) { d.goldValue = std::max(0, d.goldValue); dirty_ = true; }
    if (ImGui::Checkbox("Stackable", &d.stackable)) dirty_ = true;
    if (d.stackable && ImGui::InputInt("Max Stack", &d.maxStack)) {
        d.maxStack = std::max(1, d.maxStack); dirty_ = true;
    }

    if (d.type == ItemType::Weapon || d.type == ItemType::Armor) {
        ImGui::SeparatorText("Equipment bonuses");
        if (ImGui::InputInt("Bonus Attack", &d.bonusAttack)) dirty_ = true;
        if (ImGui::InputInt("Bonus Defense", &d.bonusDefense)) dirty_ = true;
        if (ImGui::InputInt("Bonus Magic Attack", &d.bonusMagicAttack)) dirty_ = true;
        if (ImGui::InputFloat("Bonus Speed", &d.bonusSpeed)) dirty_ = true;
        if (ImGui::InputFloat("Bonus Crit Chance", &d.bonusCritChance, 0.01f)) dirty_ = true;
        if (ImGui::InputInt("Bonus Max HP", &d.bonusMaxHp)) dirty_ = true;
        if (ImGui::InputInt("Bonus Max Mana", &d.bonusMaxMana)) dirty_ = true;
    } else if (d.type == ItemType::Consumable) {
        ImGui::SeparatorText("Consumable effect");
        if (ImGui::InputText("Effect", effectBuf, sizeof(effectBuf))) {
            d.consumableEffect = effectBuf; dirty_ = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(e.g. heal, restore_mana)");
        if (ImGui::InputInt("Value", &d.consumableValue)) dirty_ = true;
    }
}
