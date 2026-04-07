#include "AssetInspectorPanel.h"
#include "AssetTypes.h"
#include "imgui.h"
#include <ctime>
#include <vector>

void AssetInspectorPanel::draw(const std::string& selectedGuid,
                               AssetDatabase& db,
                               ImportManager& importMgr,
                               const std::string& assetsRoot,
                               const std::string& libraryRoot,
                               const std::string& dbPath,
                               LogCallback logCb)
{
    ImGui::Begin("Asset Inspector");

    if (selectedGuid.empty()) {
        ImGui::TextDisabled("Select an asset in the Asset Browser.");
        ImGui::End();
        return;
    }

    const AssetRecord* rec = db.findByGuid(selectedGuid);
    if (!rec) {
        ImGui::TextDisabled("Asset not found (GUID: %s)", selectedGuid.c_str());
        ImGui::End();
        return;
    }

    // Header
    ImGui::TextColored({0.6f, 0.9f, 0.6f, 1.f}, "%s", rec->sourcePath.c_str());
    ImGui::Separator();

    // GUID
    ImGui::Text("GUID:");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", rec->guid.c_str());

    // Type
    ImGui::Text("Type:");
    ImGui::SameLine();
    ImGui::Text("%s", assetTypeToStr(rec->assetType));

    // Hash
    ImGui::Text("Hash:");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", rec->hash.c_str());

    // Import path
    ImGui::Text("Import Path:");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", rec->importPath.c_str());

    // Last import time
    if (rec->lastImportTime > 0) {
        std::time_t t = static_cast<std::time_t>(rec->lastImportTime);
        char timeBuf[64];
        std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        ImGui::Text("Last Import:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", timeBuf);
    }

    // Dependencies
    if (!rec->dependencies.empty()) {
        ImGui::Separator();
        ImGui::Text("Dependencies (%zu):", rec->dependencies.size());
        for (auto& depGuid : rec->dependencies) {
            const AssetRecord* dep = db.findByGuid(depGuid);
            if (dep)
                ImGui::BulletText("%s", dep->sourcePath.c_str());
            else
                ImGui::BulletText("%s (missing)", depGuid.c_str());
        }
    }

    // Reimport button
    ImGui::Separator();
    if (ImGui::Button("Reimport")) {
        std::vector<std::string> errors;
        bool imported = importMgr.importAsset(
            assetsRoot, libraryRoot, rec->sourcePath, db, errors, true);
        if (logCb) {
            if (imported)
                logCb("Reimported: " + rec->sourcePath);
            else
                logCb("Reimport skipped: " + rec->sourcePath);
            for (auto& e : errors) logCb("[IMPORT] " + e);
        }
        if (imported) db.save(dbPath);
    }

    ImGui::End();
}
