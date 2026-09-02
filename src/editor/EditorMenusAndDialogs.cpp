// ═════════════════════════════════════════════════════════════════════════════
// EditorApp — menu bar, fixed toolbar, migration log modal and the
// unsaved-changes confirmation guard.
//
// Split out of EditorApp.cpp to keep that file navigable.
// ═════════════════════════════════════════════════════════════════════════════
#include "EditorApp.h"
#include "IconsFontAwesome6.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <sstream>
#include <vector>

// ═════════════════════════════════════════════════════════════════════════════
// Menu bar (inside the dockspace host window)
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawMenuBar()
{
    if (!ImGui::BeginMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        // ── Project ──────────────────────────────────────────────────────────
        if (ImGui::MenuItem("New Project...", "Ctrl+Shift+N"))
            welcomePanel_.open();
        if (ImGui::MenuItem("Open Project...", "Ctrl+Shift+O"))
            welcomePanel_.open();
        ImGui::Separator();
        // ── Scene ─────────────────────────────────────────────────────────────
        if (ImGui::MenuItem(ICON_FA_FILE " New Scene",     "Ctrl+N"))
            requestAction(PendingAction::NewScene);
        if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Open Scene...", "Ctrl+O"))
            requestAction(PendingAction::OpenScene);
        if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK " Save",    "Ctrl+S")) {
            if (scene_.filePath.empty()) showSaveDialog_ = true;
            else saveScene(scene_.filePath);
        }
        if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK " Save As...")) showSaveDialog_ = true;
        ImGui::Separator();
        // ── Build ─────────────────────────────────────────────────────────────
        if (ImGui::MenuItem("Export Bundle",
                            nullptr, false,
                            projectManager_.hasActiveProject()))
            exportGameBundle();
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_DOOR_OPEN " Exit"))
            requestAction(PendingAction::Exit);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        ImGui::SeparatorText("Scene");
        ImGui::MenuItem("Viewport", nullptr, &showViewport_);
        ImGui::MenuItem("Scene Hierarchy", nullptr, &showSceneHierarchy_);
        ImGui::MenuItem("Scene Selector", nullptr, &showSceneSelector_);
        ImGui::MenuItem("Tile Palette", nullptr, &showTilePalette_);
        ImGui::MenuItem("Biome Designer", nullptr, &showBiomeDesignerPanel_);
        ImGui::MenuItem("Entity Viewport", nullptr, &showEntityViewport_);

        ImGui::SeparatorText("Details");
        ImGui::MenuItem("Properties", nullptr, &showPropertiesPanel_);
        ImGui::MenuItem("Lighting", nullptr, &showLightingPanel_);
        ImGui::MenuItem("Audio", nullptr, &showAudioPanel_);

        ImGui::SeparatorText("Content");
        ImGui::MenuItem("Asset Browser", nullptr, &showAssetBrowser_);
        ImGui::MenuItem("Asset Inspector", nullptr, &showAssetInspector_);
        ImGui::MenuItem("File Browser", nullptr, &showFileBrowser_);
        ImGui::MenuItem("File Editor", nullptr, &showFileEditor_);
        ImGui::MenuItem("Bone Structure", nullptr, &showBoneStructurePanel_);
        ImGui::MenuItem("Animation", nullptr, &showAnimationPanel_);
        ImGui::MenuItem("State Machine", nullptr, &showStateMachinePanel_);
        ImGui::Separator();
        ImGui::MenuItem("Items", nullptr, &showItemsPanel_);
        ImGui::MenuItem("Bestiary", nullptr, &showBestiaryPanel_);
        ImGui::MenuItem("Classes", nullptr, &showClassesPanel_);
        ImGui::MenuItem("Settlements", nullptr, &showSettlementPanel_);

        ImGui::SeparatorText("Diagnostics");
        ImGui::MenuItem("Build Log", nullptr, &showBuildLog_);
        ImGui::MenuItem("Performance", nullptr, &showPerformancePanel_);
        ImGui::MenuItem("Validation", nullptr, &showValidationPanel_);
        ImGui::MenuItem("Runtime Inspector", nullptr, &showRuntimeInspector_);

        ImGui::SeparatorText("Layout");
        ImGui::MenuItem("Toolbar", nullptr, &showToolbar_);
        // Cleared here, rebuilt by the dockspace host on the next frame.
        if (ImGui::MenuItem("Reset Layout")) layoutInitialized_ = false;

        ImGui::Separator();
        ImGui::MenuItem("Auto-Reload Assets", nullptr, &autoReload_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Assets")) {
        if (ImGui::MenuItem(ICON_FA_PAINTBRUSH " Sprite Editor")) spriteEditor_.isOpen = true;
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_ARROWS_ROTATE " Scan for Changes")) {
            // Force immediate scan regardless of poll interval
            fileWatcher_ = FileWatcher(assetsRoot_, 0.0f);
            fileWatcher_.scan();
            const auto& fwChanges = fileWatcher_.changes();
            if (fwChanges.empty()) {
                addLog("[Hot-Reload] No changes detected.");
            } else {
                std::vector<std::string> errs;
                bool dbChanged = importManager_.reimportChanged(
                    fwChanges, assetsRoot_, libraryRoot_, assetDb_, errs);
                for (const auto& ch : fwChanges)
                    addLog("[Hot-Reload] Reimported: " + ch.relativePath);
                for (const auto& err : errs)
                    addLog("[IMPORT] " + err);
                if (dbChanged) {
                    assetDb_.save(assetDbPath_);
                }
            }
            // Restore normal watcher
            fileWatcher_ = FileWatcher(assetsRoot_, 1.0f);
            fileWatcher_.reset();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        std::string undoLabel = ICON_FA_ROTATE_LEFT " Undo";
        if (commandStack_.canUndo())
            undoLabel += std::string(" (") + commandStack_.undoName() + ")";
        if (ImGui::MenuItem(undoLabel.c_str(), "Cmd+Z", false, commandStack_.canUndo()))
            performUndo();

        std::string redoLabel = ICON_FA_ROTATE_RIGHT " Redo";
        if (commandStack_.canRedo())
            redoLabel += std::string(" (") + commandStack_.redoName() + ")";
        if (ImGui::MenuItem(redoLabel.c_str(), "Cmd+Shift+Z", false, commandStack_.canRedo()))
            performRedo();

        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem(ICON_FA_SHIELD_HALVED " Validate Scene", "Shift+V")) {
            validationIssues_ = contentValidator_.validate(scene_, world_, assetDb_);
            showValidationPanel_ = true;
            addLog("Validation: " + std::to_string(validationIssues_.size()) + " issue(s) found.");
        }
        if (ImGui::MenuItem(ICON_FA_DATABASE " Migrate Project Data to SQLite",
                            nullptr,
                            false,
                            projectManager_.hasActiveProject())) {
            const bool ok = projectManager_.migrateProjectDataToSqlite(true);
            const auto& migration = projectManager_.lastMigrationStatus();
            migrationLastSuccess_ = migration.success;
            if (ok) {
                addLog("[MIGRATION] Manual migration completed: " + migration.dbPath);
            } else {
                addLog("[MIGRATION] Manual migration failed; JSON fallback remains active.");
            }

            std::ostringstream summary;
            summary << "Result: " << (migration.success ? "SUCCESS" : "FAILED") << "\n";
            summary << "Duration: " << migration.summary.elapsedMs << " ms\n";
            if (!migration.dbPath.empty())
                summary << "Database: " << migration.dbPath << "\n";
            summary << "Errors: " << migration.summary.errorCount << "\n";
            summary << "\nMigrated tables (rows):\n";
            summary << "scenes: " << migration.summary.scenes << "\n";
            summary << "assets: " << migration.summary.assets << "\n";
            summary << "asset_dependencies: " << migration.summary.assetDependencies << "\n";
            summary << "player_classes: " << migration.summary.playerClasses << "\n";
            summary << "enemies: " << migration.summary.enemies << "\n";
            summary << "loot_tables: " << migration.summary.lootTables << "\n";
            summary << "loot_table_enemies: " << migration.summary.lootEnemyLinks << "\n";
            summary << "loot_drops: " << migration.summary.lootDrops << "\n";
            migrationSummaryText_ = summary.str();

            std::ostringstream os;
            os << "Result: " << (migration.success ? "SUCCESS" : "FAILED") << "\n";
            if (!migration.dbPath.empty())
                os << "Database: " << migration.dbPath << "\n";
            os << "\nDetailed log:\n";
            for (const auto& line : migration.log)
                os << line << "\n";

            migrationLogText_ = os.str();
            showMigrationLogModal_ = true;

            refreshSceneFiles();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem(ICON_FA_CIRCLE_INFO " About DashEngine"))
            showAboutModal_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void EditorApp::drawMigrationLogModal()
{
    if (showMigrationLogModal_) {
        ImGui::OpenPopup("SQLite Migration Log");
        showMigrationLogModal_ = false;
    }

    if (ImGui::BeginPopupModal("SQLite Migration Log", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImVec4 statusColor = migrationLastSuccess_
            ? ImVec4(0.22f, 0.75f, 0.22f, 1.0f)
            : ImVec4(0.86f, 0.30f, 0.24f, 1.0f);
        ImGui::TextWrapped("Manual migration output.");
        ImGui::TextColored(statusColor, "%s", migrationLastSuccess_ ? "SUCCESS" : "FAILED");
        ImGui::Separator();

        std::vector<char> summaryBuffer(migrationSummaryText_.begin(), migrationSummaryText_.end());
        summaryBuffer.push_back('\0');
        ImGui::InputTextMultiline(
            "##migration_summary",
            summaryBuffer.data(),
            summaryBuffer.size(),
            ImVec2(760.0f, 180.0f),
            ImGuiInputTextFlags_ReadOnly);

        ImGui::Separator();
        ImGui::TextWrapped("Detailed log (select and copy):");

        ImVec2 size(760.0f, 340.0f);
        std::vector<char> logBuffer(migrationLogText_.begin(), migrationLogText_.end());
        logBuffer.push_back('\0');
        ImGui::InputTextMultiline(
            "##migration_log",
            logBuffer.data(),
            logBuffer.size(),
            size,
            ImGuiInputTextFlags_ReadOnly);

        if (ImGui::Button("Close", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawToolbar()
{
    // A fixed strip under the menu bar rather than a dockable panel: the Play
    // and Stop buttons must not be something the user can drag away or lose.
    constexpr float kToolbarHeight = 46.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {6.0f, 6.0f});
    ImGui::BeginChild("##MainToolbar", {0.0f, kToolbarHeight},
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();

    // ▶ Build & Run (green)
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.220f, 0.541f, 0.204f, 1.f}); // #388A34
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  {0.298f, 0.686f, 0.314f, 1.f}); // #4CAF50
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   {0.180f, 0.490f, 0.196f, 1.f});
    if (ImGui::Button(ICON_FA_HAMMER "  Build & Run  ", {170, 34})) buildAndRun();
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // ▶ Play / ■ Stop (in-editor play mode)
    if (editorMode_ == EditorMode::Edit) {
        ImGui::PushStyleColor(ImGuiCol_Button,       {0.000f, 0.478f, 0.800f, 1.f}); // #007ACC
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.067f, 0.467f, 0.733f, 1.f}); // #1177BB
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.055f, 0.388f, 0.612f, 1.f}); // #0E639C
        if (ImGui::Button(ICON_FA_PLAY "  Play  ", {110, 34})) enterPlayMode();
        ImGui::PopStyleColor(3);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button,       {0.957f, 0.278f, 0.278f, 1.f}); // #F44747
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.827f, 0.184f, 0.184f, 1.f}); // #D32F2F
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.710f, 0.150f, 0.150f, 1.f});
        if (ImGui::Button(ICON_FA_STOP "  Stop  ", {110, 34})) exitPlayMode();
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        drawPlaybackControls();
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // Tool buttons (disabled in Play mode)
    bool inEdit = (editorMode_ == EditorMode::Edit);
    auto toolBtn = [&](const char* label, Tool t) {
        bool sel = (currentTool_ == t);
        if (!inEdit) ImGui::BeginDisabled();
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, {0.035f, 0.278f, 0.443f, 1.f}); // #094771
        if (ImGui::Button(label, {110, 34})) currentTool_ = t;
        if (sel) ImGui::PopStyleColor();
        if (!inEdit) ImGui::EndDisabled();
        ImGui::SameLine();
    };

    toolBtn(ICON_FA_ARROW_POINTER " Select",      Tool::Select);
    toolBtn(ICON_FA_SKULL         " Place Enemy",  Tool::PlaceEnemy);
    toolBtn(ICON_FA_ERASER        " Erase",        Tool::Erase);

    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // ✔ Validate Scene
    ImGui::PushStyleColor(ImGuiCol_Button,        {0.773f, 0.525f, 0.753f, 1.f}); // #C586C0
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  {0.808f, 0.576f, 0.847f, 1.f}); // #CE93D8
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   {0.690f, 0.440f, 0.680f, 1.f});
    if (ImGui::Button(ICON_FA_SHIELD_HALVED "  Validate  ", {130, 34})) {
        validationIssues_ = contentValidator_.validate(scene_, world_, assetDb_);
        showValidationPanel_ = true;
        addLog("Validation: " + std::to_string(validationIssues_.size()) + " issue(s) found.");
    }
    ImGui::PopStyleColor(3);

    ImGui::EndChild();
}

// ═════════════════════════════════════════════════════════════════════════════
// Unsaved-changes guard
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::requestAction(PendingAction action)
{
    if (scene_.modified) {
        pendingAction_     = action;
        showConfirmDialog_ = true;
    } else {
        pendingAction_ = action;
        executePendingAction();
    }
}

void EditorApp::executePendingAction()
{
    PendingAction action = pendingAction_;
    pendingAction_     = PendingAction::None;
    showConfirmDialog_ = false;

    switch (action) {
    case PendingAction::NewScene:
        newScene();
        break;
    case PendingAction::OpenScene:
        refreshSceneFiles();
        showOpenDialog_ = true;
        break;
    case PendingAction::Exit:
        running_ = false;
        break;
    case PendingAction::None:
        break;
    }
}

void EditorApp::drawConfirmDialog()
{
    ImGui::OpenPopup("Unsaved Changes");
    if (ImGui::BeginPopupModal("Unsaved Changes", &showConfirmDialog_,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("The current scene has unsaved changes.");
        ImGui::Text("Do you want to save before continuing?");
        ImGui::Separator();

        if (ImGui::Button("Save", {100, 0})) {
            if (scene_.filePath.empty())
                showSaveDialog_ = true;
            else
                saveScene(scene_.filePath);
            executePendingAction();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", {100, 0})) {
            executePendingAction();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {100, 0})) {
            pendingAction_     = PendingAction::None;
            showConfirmDialog_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
