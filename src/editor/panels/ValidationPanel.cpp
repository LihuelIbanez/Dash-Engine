#include "ValidationPanel.h"
#include "imgui.h"
#include <cstdio>

void ValidationPanel::draw(const std::vector<ValidationIssue>& issues,
                            uint64_t& selectedEntityId,
                            float& camX,
                            float& camY,
                            RefreshCallback refreshCb)
{
    ImGui::Begin("Validation");

    // ── Header: counters + Refresh button ────────────────────────────────────
    int errCount  = 0;
    int warnCount = 0;
    for (const auto& i : issues) {
        if (i.severity == ValidationIssue::Severity::Error)   ++errCount;
        else                                                    ++warnCount;
    }

    if (errCount > 0)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.3f, 0.3f, 1.f));
    else
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.f));
    ImGui::Text("%d Error%s", errCount, errCount == 1 ? "" : "s");
    ImGui::PopStyleColor();

    ImGui::SameLine();

    if (warnCount > 0)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.85f, 0.2f, 1.f));
    else
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.f));
    ImGui::Text("%d Warning%s", warnCount, warnCount == 1 ? "" : "s");
    ImGui::PopStyleColor();

    ImGui::SameLine();
    if (ImGui::Button("Refresh") && refreshCb)
        refreshCb();

    if (issues.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("No issues found. Scene is valid.");
        ImGui::End();
        return;
    }

    ImGui::Separator();

    // ── Issue table ───────────────────────────────────────────────────────────
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (ImGui::BeginTable("##validation", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
            avail)) {
        ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed, 70.f);
        ImGui::TableSetupColumn("Message",  ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Entity / Tile", ImGuiTableColumnFlags_WidthFixed, 100.f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(issues.size()); ++i) {
            const auto& issue = issues[static_cast<size_t>(i)];
            bool isError = (issue.severity == ValidationIssue::Severity::Error);

            ImGui::TableNextRow();
            ImGui::PushID(i);

            // ── Col 0: Severity badge ─────────────────────────────────────────
            ImGui::TableNextColumn();
            if (isError)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.3f, 0.3f, 1.f));
            else
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.85f, 0.2f, 1.f));
            ImGui::Text("%s", isError ? "[ERR] " : "[WARN]");
            ImGui::PopStyleColor();

            // ── Col 1: Message (selectable) ───────────────────────────────────
            ImGui::TableNextColumn();
            bool selected = false;
            ImGuiSelectableFlags flags =
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
            if (ImGui::Selectable(issue.message.c_str(), selected, flags)) {
                // Navigate to entity
                if (issue.entityId != 0) {
                    selectedEntityId = issue.entityId;
                    // Camera: we don't know world pos here, caller will handle it
                }
                // Navigate to tile (centre in world coords; tile is 1×1 in world units)
                if (issue.tileX >= 0 && issue.tileY >= 0) {
                    camX = static_cast<float>(issue.tileX) + 0.5f;
                    camY = static_cast<float>(issue.tileY) + 0.5f;
                }
            }

            // ── Col 2: Entity / Tile reference ────────────────────────────────
            ImGui::TableNextColumn();
            char ref[32] = "-";
            if (issue.entityId != 0)
                std::snprintf(ref, sizeof(ref), "E:%llu",
                              static_cast<unsigned long long>(issue.entityId));
            else if (issue.tileX >= 0)
                std::snprintf(ref, sizeof(ref), "T:%d,%d", issue.tileX, issue.tileY);
            ImGui::TextDisabled("%s", ref);

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::End();
}
