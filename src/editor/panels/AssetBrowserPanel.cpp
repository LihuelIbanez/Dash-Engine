#include "AssetBrowserPanel.h"
#include "AssetTypes.h"
#include "imgui.h"
#include <algorithm>

void AssetBrowserPanel::draw(AssetDatabase& db,
                             ImportManager& importMgr,
                             const std::string& assetsRoot,
                             const std::string& libraryRoot,
                             const std::string& dbPath,
                             LogCallback logCb)
{
    ImGui::Begin("Asset Browser");

    // Toolbar row
    if (ImGui::Button("Reimport All")) {
        std::vector<std::string> errors;
        int count = importMgr.importAll(assetsRoot, libraryRoot, db, errors);
        if (logCb) {
            logCb("Reimported " + std::to_string(count) + " asset(s).");
            for (auto& e : errors) logCb("[IMPORT] " + e);
        }
        if (count > 0) db.save(dbPath);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu assets)", db.records().size());

    // Filter
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Filter...", filterBuf_, sizeof(filterBuf_));
    ImGui::Separator();

    std::string filter(filterBuf_);
    // Lowercase filter for case-insensitive match
    std::string filterLower = filter;
    for (auto& c : filterLower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Table of assets
    if (ImGui::BeginTable("##assets", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
            | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY
            | ImGuiTableFlags_SizingFixedFit,
            ImGui::GetContentRegionAvail())) {

        ImGui::TableSetupColumn("Source Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type",        ImGuiTableColumnFlags_WidthFixed, 90.f);
        ImGui::TableSetupColumn("GUID",        ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (auto& [guid, rec] : db.records()) {
            // Apply filter
            if (!filterLower.empty()) {
                std::string pathLower = rec.sourcePath;
                for (auto& c : pathLower)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (pathLower.find(filterLower) == std::string::npos)
                    continue;
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            bool isSelected = (selectedGuid_ == guid);
            ImGuiSelectableFlags flags = ImGuiSelectableFlags_SpanAllColumns
                                    | ImGuiSelectableFlags_AllowOverlap;
            if (ImGui::Selectable(rec.sourcePath.c_str(), isSelected, flags)) {
                selectedGuid_ = guid;
            }

            // Drag-drop source: allow dragging Prefab assets onto the viewport.
            if (rec.assetType == AssetType::Prefab && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("PREFAB_GUID", guid.c_str(), guid.size() + 1);
                ImGui::Text("Prefab: %s", rec.sourcePath.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(assetTypeToStr(rec.assetType));

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", guid.c_str());
        }

        ImGui::EndTable();
    }

    ImGui::End();
}
