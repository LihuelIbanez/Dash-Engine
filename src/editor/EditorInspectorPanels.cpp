// ═════════════════════════════════════════════════════════════════════════════
// EditorApp — Scene Hierarchy, Scene Selector, Tile Palette, Build Log,
// Performance and Lighting panels.
//
// Split out of EditorApp.cpp to keep that file navigable.
// ═════════════════════════════════════════════════════════════════════════════
#include "EditorApp.h"
#include "EraseCommand.h"
#include "IconsFontAwesome6.h"
#include "PlaceEnemyCommand.h"
#include "Profiler.h"
#include "commands/CreateEntityCommand.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ═════════════════════════════════════════════════════════════════════════════
// Scene Hierarchy
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawSceneHierarchy()
{
    ImGui::Begin("Scene Hierarchy");

    ImGui::Text("Scene: %s%s", scene_.sceneName.c_str(),
                scene_.modified ? " *" : "");
    ImGui::Separator();

    for (uint64_t rootId : dash::editor::rootEntities(scene_))
        drawHierarchyNode(rootId, 0);

    // Dropping on the empty area below the tree unparents the entity.
    ImGui::Dummy({-1, 18});
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("DASH_ENTITY")) {
            uint64_t dragged = 0;
            std::memcpy(&dragged, p->Data, sizeof(dragged));
            reparentEntity(dragged, 0);
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::Separator();
    if (editorMode_ == EditorMode::Play) ImGui::BeginDisabled();
    if (ImGui::Button("+ Add Enemy", {-1, 0})) {
        uint64_t newId = scene_.allocateEntityId();
        auto cmd = std::make_unique<PlaceEnemyCommand>(camX_, camY_, newId, "NewEnemy");
        commandStack_.execute(std::move(cmd), scene_, world_);
        setSelection(newId);
        addLog("Entity added.");
    }
    if (ImGui::Button("+ Add Light", {-1, 0})) {
        EntityData light;
        light.id = scene_.allocateEntityId();
        light.type = EntityData::Type::Enemy;  // scene entity types are Player/Enemy only
        light.name = "Light";
        light.x = camX_;
        light.y = camY_;
        TransformComponent tf;
        tf.x = camX_; tf.y = camY_; tf.z = 2.0f;
        light.components.push_back(tf);
        light.components.push_back(LightComponent{});
        const uint64_t newId = light.id;
        commandStack_.execute(
            std::make_unique<CreateEntityCommand>(std::move(light), "Create Light"),
            scene_, world_);
        setSelection(newId);
        addLog("Light added.");
    }

    EntityData* sel = findEntityById(selectedEntityId_);
    if (sel && sel->type != EntityData::Type::Player) {
        if (ImGui::Button("- Remove Selected", {-1, 0})) {
            auto cmd = std::make_unique<EraseCommand>(selectedEntityId_);
            commandStack_.execute(std::move(cmd), scene_, world_);
            clearSelection();
        }
    }
    if (editorMode_ == EditorMode::Play) ImGui::EndDisabled();

    ImGui::End();
}

void EditorApp::drawSceneSelector()
{
    ImGui::Begin("Scene Selector");

    if (!projectManager_.hasActiveProject()) {
        ImGui::TextDisabled("Open a project to browse scenes.");
        ImGui::End();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.000f, 0.478f, 0.800f, 1.f)); // #007ACC
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.067f, 0.467f, 0.733f, 1.f)); // #1177BB
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.055f, 0.388f, 0.612f, 1.f));
    if (ImGui::Button(ICON_FA_ARROWS_ROTATE " Refresh", {120, 0})) {
        refreshSceneFiles();
    }
    ImGui::PopStyleColor(3);

    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.220f, 0.541f, 0.204f, 1.f)); // #388A34
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.298f, 0.686f, 0.314f, 1.f)); // #4CAF50
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.180f, 0.490f, 0.196f, 1.f));
    if (ImGui::Button(ICON_FA_FILE " Create", {120, 0})) {
        std::strncpy(createSceneFileName_, "new_scene.json", sizeof(createSceneFileName_));
        createSceneFileName_[sizeof(createSceneFileName_) - 1] = '\0';
        showCreateSceneDialog_ = true;
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.808f, 0.569f, 0.471f, 1.f)); // #CE9178
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.878f, 0.639f, 0.541f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.710f, 0.490f, 0.400f, 1.f));
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " Open", {120, 0})) {
        if (!selectedSceneFile_.empty()) {
            openScene(selectedSceneFile_);
        }
    }
    ImGui::PopStyleColor(3);

    ImGui::Separator();
    ImGui::TextDisabled(ICON_FA_FILE " Project scenes:");

    if (ImGui::BeginListBox("##scene_selector_list", ImVec2(-FLT_MIN, -FLT_MIN))) {
        if (sceneFiles_.empty()) {
            ImGui::Selectable("(No scene files in scenes/)", false, ImGuiSelectableFlags_Disabled);
        } else {
            for (const auto& sceneFile : sceneFiles_) {
                const bool isSelected = (sceneFile == selectedSceneFile_);
                std::string label = std::string(ICON_FA_FILE " ") + sceneFile + "##" + sceneFile;
                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    selectedSceneFile_ = sceneFile;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        openScene(sceneFile);
                    }
                }
            }
        }
        ImGui::EndListBox();
    }

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════════
// Tile Palette
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawTilePalette()
{
    ImGui::Begin("Tile Palette");

    // ── Map tool buttons (icon-only, 28×28) ─────────────────────────────────
    {
        auto mapToolBtn = [&](const char* icon, Tool t, const char* tip) {
            bool active = (currentTool_ == t);
            if (active) ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(icon, {28, 28})) currentTool_ = t;
            if (active) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            ImGui::SameLine();
        };

        mapToolBtn(ICON_FA_PAINTBRUSH,  Tool::PaintTile,    "Paint Tile");
        mapToolBtn(ICON_FA_FILL_DRIP,   Tool::FillTile,    "Flood Fill");
        mapToolBtn(ICON_FA_EYE_DROPPER, Tool::EyeDropper,  "Eyedropper (Pick Tile)");
        mapToolBtn(ICON_FA_MOUNTAIN,    Tool::HeightBrush,  "Height Brush (Sculpt)");
        mapToolBtn(ICON_FA_STAIRS,      Tool::CliffBrush,   "Cliff Brush (WC3)");
        mapToolBtn(ICON_FA_PALETTE,     Tool::TexturePaint, "Texture Paint");
        mapToolBtn(ICON_FA_WATER,       Tool::WaterTool,    "Water Tool");
        mapToolBtn(ICON_FA_ERASER,      Tool::Erase,        "Erase (Reset to Grass)");
        ImGui::NewLine();
    }

    // ── Brush size slider (visible for PaintTile) ───────────────────────────
    if (currentTool_ == Tool::PaintTile) {
        ImGui::Text(ICON_FA_BRUSH " Size");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##brush", &brushSize_, 1, 5);
    }

    // ── Height brush settings ───────────────────────────────────────────────
    if (currentTool_ == Tool::HeightBrush) {
        const char* modeNames[] = {"Raise", "Lower", "Smooth", "Flatten"};
        int modeIdx = static_cast<int>(heightBrushMode_);
        ImGui::Text("Mode");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::Combo("##hbmode", &modeIdx, modeNames, 4))
            heightBrushMode_ = static_cast<HeightBrushMode>(modeIdx);

        ImGui::Text("Radius");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##hbradius", &heightBrushRadius_, 1, 8);

        ImGui::Text("Strength");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderFloat("##hbstr", &heightBrushStrength_, 0.01f, 0.20f, "%.3f");
    }

    // ── Cliff brush settings ────────────────────────────────────────────────
    if (currentTool_ == Tool::CliffBrush) {
        const char* cliffModes[] = {"Raise", "Lower"};
        int cmi = (cliffBrushMode_ == CliffBrushCommand::Mode::Raise) ? 0 : 1;
        ImGui::Text("Mode");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::Combo("##cbmode", &cmi, cliffModes, 2))
            cliffBrushMode_ = (cmi == 0) ? CliffBrushCommand::Mode::Raise
                                         : CliffBrushCommand::Mode::Lower;

        ImGui::Text("Radius");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##cbradius", &cliffBrushRadius_, 1, 5);
    }

    // ── Texture paint settings ──────────────────────────────────────────────
    if (currentTool_ == Tool::TexturePaint) {
        struct TexInfo { const char* name; TerrainTextureId id; ImVec4 col; };
        TexInfo textures[] = {
            {"Grass",     TerrainTextureId::Grass,     {0.24f, 0.47f, 0.16f, 1.f}},
            {"Dirt",      TerrainTextureId::Dirt,       {0.43f, 0.29f, 0.16f, 1.f}},
            {"Rock",      TerrainTextureId::Rock,       {0.47f, 0.45f, 0.41f, 1.f}},
            {"Sand",      TerrainTextureId::Sand,       {0.71f, 0.63f, 0.35f, 1.f}},
            {"Snow",      TerrainTextureId::Snow,       {0.86f, 0.88f, 0.92f, 1.f}},
            {"Mud",       TerrainTextureId::Mud,        {0.35f, 0.22f, 0.10f, 1.f}},
            {"Dark Grass",TerrainTextureId::DarkGrass,  {0.12f, 0.29f, 0.10f, 1.f}},
            {"Gravel",    TerrainTextureId::Gravel,     {0.55f, 0.53f, 0.49f, 1.f}},
            {"Ice",       TerrainTextureId::Ice,        {0.72f, 0.85f, 0.93f, 1.f}},
        };
        ImGui::Text("Texture:");
        const float texBtnSz = 24.f;
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        float aw = ImGui::GetContentRegionAvail().x;
        int tcols = std::max(1, (int)((aw + sp) / (texBtnSz + sp)));
        int ti = 0;
        for (auto& tx : textures) {
            bool sel = (selectedTexture_ == tx.id);
            ImGui::PushID(100 + ti);
            ImGui::PushStyleColor(ImGuiCol_Button, tx.col);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                {tx.col.x + 0.12f, tx.col.y + 0.12f, tx.col.z + 0.12f, 1.f});
            if (sel) {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
                ImGui::PushStyleColor(ImGuiCol_Border, {1.f, 1.f, 0.f, 1.f});
            }
            if (ImGui::Button("##tex", {texBtnSz, texBtnSz}))
                selectedTexture_ = tx.id;
            if (sel) {
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tx.name);
            ++ti;
            if (ti % tcols != 0) ImGui::SameLine();
            ImGui::PopID();
        }
        if (ti % tcols != 0) ImGui::NewLine();

        ImGui::Text("Strength");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderFloat("##tpstr", &texturePaintStrength_, 0.1f, 1.0f, "%.2f");

        ImGui::Text("Radius");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##tpradius", &texturePaintRadius_, 1, 8);
    }

    // ── Water tool settings ─────────────────────────────────────────────────
    if (currentTool_ == Tool::WaterTool) {
        ImGui::Text("Water Level");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderFloat("##wlevel", &waterLevel_, 0.0f, 8.0f, "%.1f");

        ImGui::Text("Body ID");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##wbody", &selectedWaterBodyId_, 1, 8);

        if (ImGui::Button("Set Water Level")) {
            auto& bodies = world_.terrain().waterBodies();
            for (auto& wb : bodies) {
                if (wb.id == static_cast<uint8_t>(selectedWaterBodyId_)) {
                    float oldLevel = wb.waterLevel;
                    if (oldLevel != waterLevel_) {
                        auto cmd = std::make_unique<WaterLevelCommand>(
                            wb.id, oldLevel, waterLevel_);
                        commandStack_.execute(std::move(cmd), scene_, world_);
                    }
                    break;
                }
            }
        }
    }

    ImGui::Separator();

    // ── Tile grid (compact color buttons) ───────────────────────────────────
    struct TileInfo { const char* name; TileType type; ImVec4 col; };
    TileInfo tiles[] = {
        {"Deep Water", TileType::DeepWater, {0.06f, 0.16f, 0.39f, 1.f}},
        {"Water",      TileType::Water,     {0.12f, 0.27f, 0.55f, 1.f}},
        {"Sand",       TileType::Sand,      {0.71f, 0.63f, 0.35f, 1.f}},
        {"Grass",      TileType::Grass,     {0.24f, 0.47f, 0.16f, 1.f}},
        {"Forest",     TileType::Forest,    {0.12f, 0.29f, 0.10f, 1.f}},
        {"Dirt",       TileType::Dirt,      {0.43f, 0.29f, 0.16f, 1.f}},
        {"Stone",      TileType::Stone,     {0.47f, 0.45f, 0.41f, 1.f}},
        {"Mountain",   TileType::Mountain,  {0.37f, 0.33f, 0.31f, 1.f}},
        {"Snow",       TileType::Snow,      {0.86f, 0.88f, 0.92f, 1.f}},
    };

    const float btnSize = 28.f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    float availW = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)((availW + spacing) / (btnSize + spacing)));

    const char* selectedName = "None";
    int idx = 0;
    for (auto& t : tiles) {
        bool sel = (selectedTileType_ == t.type);
        if (sel) selectedName = t.name;

        ImGui::PushID(idx);
        ImGui::PushStyleColor(ImGuiCol_Button, t.col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            {t.col.x + 0.12f, t.col.y + 0.12f, t.col.z + 0.12f, 1.f});

        if (sel) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.f);
            ImGui::PushStyleColor(ImGuiCol_Border, {1.f, 1.f, 0.f, 1.f});
        }

        if (ImGui::Button("##tile", {btnSize, btnSize})) {
            selectedTileType_ = t.type;
            if (currentTool_ != Tool::PaintTile &&
                currentTool_ != Tool::FillTile)
                currentTool_ = Tool::PaintTile;
        }

        if (sel) {
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
        ImGui::PopStyleColor(2);

        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", t.name);

        ++idx;
        if (idx % cols != 0)
            ImGui::SameLine();
        ImGui::PopID();
    }

    // ── Selected tile info ──────────────────────────────────────────────────
    ImGui::Separator();
    ImGui::TextDisabled("Selected: %s", selectedName);

    // ── Terrain Rendering settings ─────────────────────────────────────────
    if (ImGui::CollapsingHeader("Terrain Rendering")) {
        ImGui::Text("Height Scale");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderFloat("##hscale", &viewport3D_.heightScale, 12.0f, 72.0f, "%.0f");

        ImGui::Text("Grid Opacity");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderFloat("##gopacity", &viewport3D_.gridOpacity, 0.0f, 1.0f, "%.2f");

        ImGui::Checkbox("Distance Fog", &viewport3D_.fogEnabled);
        if (viewport3D_.fogEnabled) {
            ImGui::Text("Fog Start");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::SliderFloat("##fogstart", &viewport3D_.fogStart, 10.0f, 100.0f, "%.0f");

            ImGui::Text("Fog End");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::SliderFloat("##fogend", &viewport3D_.fogEnd, 20.0f, 200.0f, "%.0f");
        }
    }

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════════
// Build Log
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawBuildLog()
{
    ImGui::Begin("Build Log");

    if (ImGui::CollapsingHeader("Play Audit (ultimas 2 sesiones)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const std::string auditPath = playAuditFilePath();
        ImGui::TextDisabled("Archivo: %s", auditPath.c_str());

        json auditRoot;
        auditRoot["sessions"] = json::array();

        std::ifstream in(auditPath);
        if (in.is_open()) {
            try {
                in >> auditRoot;
            } catch (...) {
                auditRoot = json{};
                auditRoot["sessions"] = json::array();
            }
        }

        const bool hasSessions = auditRoot.contains("sessions")
                                 && auditRoot["sessions"].is_array()
                                 && !auditRoot["sessions"].empty();

        if (!hasSessions) {
            ImGui::TextDisabled("Sin sesiones auditadas todavia.");
        } else {
            const auto& sessions = auditRoot["sessions"];
            int shown = 0;
            for (int i = static_cast<int>(sessions.size()) - 1; i >= 0 && shown < 2; --i, ++shown) {
                const auto& s = sessions[static_cast<size_t>(i)];
                const std::string startedAt = s.value("startedAt", "unknown");
                const std::string endedAt = s.value("endedAt", "unknown");
                const std::string reason = s.value("reason", "unknown");

                const std::string title = "Sesion " + std::to_string(shown + 1)
                                        + " | " + startedAt
                                        + "##audit_session_" + std::to_string(i);
                if (ImGui::TreeNode(title.c_str())) {
                    ImGui::Text("Inicio: %s", startedAt.c_str());
                    ImGui::Text("Fin: %s", endedAt.c_str());
                    ImGui::Text("Motivo: %s", reason.c_str());

                    if (s.contains("logs") && s["logs"].is_array()) {
                        ImGui::SeparatorText("Logs de sesion");
                        ImGui::BeginChild(("audit_logs_" + std::to_string(i)).c_str(), ImVec2(-FLT_MIN, 140.0f), true);
                        for (const auto& line : s["logs"]) {
                            if (line.is_string()) {
                                ImGui::TextUnformatted(line.get_ref<const std::string&>().c_str());
                            }
                        }
                        ImGui::EndChild();
                    }
                    ImGui::TreePop();
                }
            }
        }

        ImGui::Separator();
    }

    ImGui::TextDisabled("Log en vivo del editor");

    std::string combinedLog;
    size_t totalSize = 0;
    for (const auto& msg : log_)
        totalSize += msg.size() + 1;
    combinedLog.reserve(totalSize);

    for (const auto& msg : log_) {
        combinedLog += msg;
        combinedLog += '\n';
    }

    const bool wasAtBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
    ImGui::InputTextMultiline("##build_log_text",
                              combinedLog.data(),
                              combinedLog.size() + 1,
                              ImVec2(-FLT_MIN, -FLT_MIN),
                              ImGuiInputTextFlags_ReadOnly);
    if (wasAtBottom)
        ImGui::SetScrollHereY(1.f);
    ImGui::End();
}

void EditorApp::drawPerformancePanel()
{
    ImGui::Begin("Performance");

    auto& prof = Profiler::instance();
    ImGui::Text("FPS: %.1f", prof.fps());
    ImGui::Text("Frame: %.2f ms  (avg %.2f ms, peak %.2f ms)",
                prof.frameDtMs(), prof.frameAvgMs(), prof.framePeakMs());

    ImGui::Separator();
    ImGui::Text("Subsystems:");

    if (ImGui::BeginTable("##PerfTable", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Section");
        ImGui::TableSetupColumn("Last (ms)");
        ImGui::TableSetupColumn("Avg (ms)");
        ImGui::TableSetupColumn("Peak (ms)");
        ImGui::TableHeadersRow();

        for (auto& s : prof.sections()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(s.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", s.lastMs);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", s.avgMs);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", s.peakMs);
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════════
// Lighting Panel
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawLightingPanel()
{
    ImGui::Begin("Lighting");

    ImGui::SeparatorText("Directional Light");

    // Direction
    float dir[3] = { viewport3D_.lightDirX, viewport3D_.lightDirY, viewport3D_.lightDirZ };
    ImGui::Text("Direction");
    if (ImGui::SliderFloat3("##lightdir", dir, -1.0f, 1.0f, "%.2f")) {
        viewport3D_.lightDirX = dir[0];
        viewport3D_.lightDirY = dir[1];
        viewport3D_.lightDirZ = dir[2];
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Normalize")) {
        float len = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
        if (len > 1e-4f) {
            viewport3D_.lightDirX = dir[0] / len;
            viewport3D_.lightDirY = dir[1] / len;
            viewport3D_.lightDirZ = dir[2] / len;
        }
    }

    // Color
    float col[3] = { viewport3D_.lightColorR, viewport3D_.lightColorG, viewport3D_.lightColorB };
    ImGui::Text("Color");
    if (ImGui::ColorEdit3("##lightcol", col)) {
        viewport3D_.lightColorR = col[0];
        viewport3D_.lightColorG = col[1];
        viewport3D_.lightColorB = col[2];
    }

    // Intensity
    ImGui::Text("Intensity");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::SliderFloat("##intensity", &viewport3D_.lightIntensity, 0.0f, 3.0f, "%.2f");

    ImGui::SeparatorText("Ambient");

    ImGui::Text("Ambient");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::SliderFloat("##ambient", &viewport3D_.ambientStrength, 0.0f, 1.0f, "%.2f");

    // Specular is no longer a global knob: it falls out of each material's
    // metallic/roughness through the Cook-Torrance BRDF.

    ImGui::Separator();
    if (ImGui::Button("Reset Defaults")) {
        viewport3D_.lightDirX = 0.3f;  viewport3D_.lightDirY = 0.9f;  viewport3D_.lightDirZ = 0.2f;
        viewport3D_.lightColorR = 1.0f; viewport3D_.lightColorG = 0.98f; viewport3D_.lightColorB = 0.92f;
        viewport3D_.lightIntensity = 1.7f;
        viewport3D_.ambientStrength = 0.30f;
    }

    ImGui::End();
}
