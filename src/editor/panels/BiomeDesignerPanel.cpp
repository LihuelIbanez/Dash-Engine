#include "BiomeDesignerPanel.h"

#include "world/BiomeTableFile.h"
#include "world/TerrainMesh.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace {

using dash::world::BiomeDef;
using dash::world::BiomeTable;

ImU32 biomeColor(const BiomeDef& b)
{
    return ImGui::ColorConvertFloat4ToU32({b.color[0], b.color[1], b.color[2], 1.0f});
}

// Cheap change detector: any edit to a range, to the preview resolution or to
// the terrain itself moves it, and only then is the map rebuilt.
uint64_t stampOf(const BiomeTable& table, const TerrainMesh& terrain, int previewSize)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    auto mixf = [&mix](float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        mix(bits);
    };

    mix(static_cast<uint64_t>(previewSize));
    mix(table.biomes.size());
    for (const BiomeDef& b : table.biomes) {
        mixf(b.elevMin); mixf(b.elevMax);
        mixf(b.moistMin); mixf(b.moistMax);
        mixf(b.color[0]); mixf(b.color[1]); mixf(b.color[2]);
        for (char c : b.id) mix(static_cast<uint64_t>(c));
    }
    const auto& elev = terrain.faceElevations();
    mix(elev.size());
    for (std::size_t i = 0; i < elev.size(); i += 1021) mixf(elev[i]);
    return h;
}

} // namespace

void BiomeDesignerPanel::draw(BiomeTable& table,
                              const TerrainMesh& terrain,
                              const std::string& assetsRoot,
                              unsigned int& worldSeed,
                              std::string& biomeTableId,
                              const RegenerateCallback& regenerate,
                              const LogCallback& log)
{
    if (!ImGui::Begin("Biome Designer")) {
        ImGui::End();
        return;
    }

    const std::string path = biomeTableId.empty()
        ? dash::world::biomeTablePath(assetsRoot)
        : (assetsRoot + "/world/biomes/" + biomeTableId + ".json");

    // ── Toolbar ─────────────────────────────────────────────────────────────
    if (ImGui::Button("Load")) {
        BiomeTable loaded;
        std::string err;
        if (dash::world::loadBiomeTableFile(path, loaded, &err)) {
            table = std::move(loaded);
            selected_ = 0;
            log("Biomes: loaded " + std::to_string(table.biomes.size()) + " biome(s) from " + path);
        } else {
            log("Biomes: load failed (" + err + ")");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        std::string err;
        if (dash::world::saveBiomeTableFile(path, table, &err))
            log("Biomes: saved " + std::to_string(table.biomes.size()) + " biome(s) to " + path);
        else
            log("Biomes: save failed (" + err + ")");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As...")) {
        std::snprintf(saveAsNameBuf_, sizeof(saveAsNameBuf_), "%s",
                      biomeTableId.empty() ? "my_biome" : biomeTableId.c_str());
        ImGui::OpenPopup("Save Biome As");
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Biome")) {
        selected_ = dash::editor::biomedesign::addBiome(table);
    }

    if (ImGui::BeginPopupModal("Save Biome As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Saves this table under assets/world/biomes/<name>.json");
        ImGui::TextUnformatted("and assigns it to the current scene.");
        ImGui::InputText("Name", saveAsNameBuf_, sizeof(saveAsNameBuf_));
        const bool validName = saveAsNameBuf_[0] != '\0';
        ImGui::BeginDisabled(!validName);
        if (ImGui::Button("Save && Assign", {160, 0})) {
            const std::string namedPath = assetsRoot + "/world/biomes/" + std::string(saveAsNameBuf_) + ".json";
            std::error_code dirEc;
            std::filesystem::create_directories(assetsRoot + "/world/biomes", dirEc);
            std::string err;
            if (dash::world::saveBiomeTableFile(namedPath, table, &err)) {
                biomeTableId = saveAsNameBuf_;
                log("Biomes: saved '" + biomeTableId + "' to " + namedPath + " and assigned it to the scene.");
                if (regenerate) regenerate(worldSeed);
            } else {
                log("Biomes: save-as failed (" + err + ")");
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {100, 0})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (biomeTableId.empty()) {
        ImGui::TextDisabled("Using default: %s", path.c_str());
    } else {
        ImGui::TextColored({0.5f, 0.8f, 1.0f, 1.0f}, "Scene biome: %s", biomeTableId.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Use Default##biome")) {
            biomeTableId.clear();
            log("Biomes: scene reverted to the default biome table.");
            if (regenerate) regenerate(worldSeed);
        }
    }

    int seed = static_cast<int>(worldSeed);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::InputInt("World Seed", &seed)) worldSeed = static_cast<unsigned int>(std::max(0, seed));
    ImGui::SameLine();
    if (ImGui::Button("Regenerate World") && regenerate) {
        regenerate(worldSeed);
        log("Biomes: regenerated world with seed " + std::to_string(worldSeed));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("Preview", &previewSize_, 32, 256);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragInt("Max plants", &table.maxVegetationInstances, 10.0f, 0, 20000);

    ImGui::Separator();

    if (ImGui::BeginChild("biome-left", {ImGui::GetContentRegionAvail().x * 0.46f, 0},
                          ImGuiChildFlags_Borders)) {
        drawPreview(table, terrain);
        drawIssues(table);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    if (ImGui::BeginChild("biome-right", {0, 0}, ImGuiChildFlags_Borders)) {
        drawBiomeEditor(table);
    }
    ImGui::EndChild();

    ImGui::End();
}

void BiomeDesignerPanel::drawPreview(const BiomeTable& table, const TerrainMesh& terrain)
{
    const uint64_t stamp = stampOf(table, terrain, previewSize_);
    if (stamp != previewGeneration_ || previewDirty_) {
        preview_ = dash::editor::biomedesign::buildPreview(
            table, terrain.faceElevations(), terrain.faceMoistures(),
            TerrainMesh::FW, TerrainMesh::FH, previewSize_);
        previewRuns_ = dash::editor::biomedesign::compressPreview(preview_);
        previewCounts_ = dash::editor::biomedesign::previewHistogram(preview_, table.biomes.size());
        previewGeneration_ = static_cast<unsigned int>(stamp);
        previewDirty_ = false;
    }

    ImGui::SeparatorText("Map Preview");
    if (preview_.width <= 0) {
        ImGui::TextDisabled("No terrain generated yet.");
        return;
    }

    const float side = std::min(ImGui::GetContentRegionAvail().x, 320.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float cell = side / static_cast<float>(preview_.width);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(origin, {origin.x + side, origin.y + cell * preview_.height},
                      IM_COL32(24, 24, 28, 255));
    for (const auto& run : previewRuns_) {
        if (run.biome < 0) continue;   // gap: left as the dark background
        const ImU32 col = biomeColor(table.biomes[static_cast<std::size_t>(run.biome)]);
        dl->AddRectFilled({origin.x + run.x0 * cell, origin.y + run.y * cell},
                          {origin.x + run.x1 * cell, origin.y + (run.y + 1) * cell},
                          col);
    }
    ImGui::Dummy({side, cell * preview_.height});
    ImGui::TextDisabled("%d x %d samples, %zu run(s)",
                        preview_.width, preview_.height, previewRuns_.size());

    ImGui::SeparatorText("Coverage");
    const int total = preview_.width * preview_.height;
    if (ImGui::BeginTable("biome-coverage", 3, ImGuiTableFlags_SizingStretchProp)) {
        for (std::size_t i = 0; i < table.biomes.size(); ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::ColorButton(("##c" + std::to_string(i)).c_str(),
                               {table.biomes[i].color[0], table.biomes[i].color[1],
                                table.biomes[i].color[2], 1.0f},
                               ImGuiColorEditFlags_NoTooltip, {14, 14});
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(table.biomes[i].id.c_str());
            ImGui::TableNextColumn();
            const int c = i < previewCounts_.size() ? previewCounts_[i] : 0;
            ImGui::Text("%d (%.1f%%)", c, total > 0 ? 100.0 * c / total : 0.0);
        }
        if (!previewCounts_.empty() && previewCounts_.back() > 0) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TableNextColumn();
            ImGui::TextColored({1.0f, 0.45f, 0.35f, 1.0f}, "<uncovered>");
            ImGui::TableNextColumn();
            ImGui::Text("%d (%.1f%%)", previewCounts_.back(),
                        total > 0 ? 100.0 * previewCounts_.back() / total : 0.0);
        }
        ImGui::EndTable();
    }

    const auto& stats = terrain.vegetationStats();
    ImGui::SeparatorText("Vegetation (last generation)");
    ImGui::Text("%d instance(s) / %d plant(s)%s", stats.instances, stats.plants,
                stats.capped ? "  [CAPPED]" : "");
    for (const auto& kv : stats.perKind)
        ImGui::BulletText("%s: %d", kv.first.c_str(), kv.second);
}

void BiomeDesignerPanel::drawIssues(const BiomeTable& table)
{
    const auto issues = dash::world::validateBiomeTable(table);
    ImGui::SeparatorText("Validation");
    if (issues.empty()) {
        ImGui::TextColored({0.4f, 0.9f, 0.4f, 1.0f}, "No gaps, overlaps or invalid ranges.");
        return;
    }
    ImGui::TextColored({1.0f, 0.6f, 0.3f, 1.0f}, "%zu problem(s):", issues.size());
    if (ImGui::BeginChild("biome-issues", {0, 120}, ImGuiChildFlags_Borders)) {
        for (const auto& is : issues) {
            const bool hard = is.kind == dash::world::BiomeIssue::Kind::Gap ||
                              is.kind == dash::world::BiomeIssue::Kind::InvalidRange ||
                              is.kind == dash::world::BiomeIssue::Kind::DuplicateId;
            ImGui::TextColored(hard ? ImVec4{1.0f, 0.45f, 0.35f, 1.0f}
                                    : ImVec4{0.95f, 0.8f, 0.4f, 1.0f},
                               "%s", is.message.c_str());
        }
    }
    ImGui::EndChild();
}

void BiomeDesignerPanel::drawBiomeEditor(BiomeTable& table)
{
    ImGui::SeparatorText("Biomes");
    if (table.biomes.empty()) {
        ImGui::TextDisabled("Empty table: generation falls back to the built-in thresholds.");
        return;
    }
    selected_ = std::max(0, std::min(selected_, static_cast<int>(table.biomes.size()) - 1));

    if (ImGui::BeginChild("biome-list", {0, 130}, ImGuiChildFlags_Borders)) {
        for (int i = 0; i < static_cast<int>(table.biomes.size()); ++i) {
            ImGui::PushID(i);
            ImGui::ColorButton("##col",
                               {table.biomes[i].color[0], table.biomes[i].color[1],
                                table.biomes[i].color[2], 1.0f},
                               ImGuiColorEditFlags_NoTooltip, {14, 14});
            ImGui::SameLine();
            if (ImGui::Selectable(table.biomes[i].id.c_str(), selected_ == i)) selected_ = i;
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("Move Up")) {
        if (dash::editor::biomedesign::moveBiome(table, selected_, -1)) --selected_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Move Down")) {
        if (dash::editor::biomedesign::moveBiome(table, selected_, 1)) ++selected_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove")) {
        dash::editor::biomedesign::removeBiome(table, selected_);
        if (table.biomes.empty()) return;
        selected_ = std::max(0, std::min(selected_, static_cast<int>(table.biomes.size()) - 1));
    }

    BiomeDef& b = table.biomes[static_cast<std::size_t>(selected_)];

    ImGui::SeparatorText("Definition");
    char idBuf[64];
    std::snprintf(idBuf, sizeof(idBuf), "%s", b.id.c_str());
    if (ImGui::InputText("id", idBuf, sizeof(idBuf))) b.id = idBuf;
    char nameBuf[96];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", b.name.c_str());
    if (ImGui::InputText("name", nameBuf, sizeof(nameBuf))) b.name = nameBuf;

    ImGui::DragFloatRange2("elevation", &b.elevMin, &b.elevMax, 0.005f, 0.0f, 1.0f,
                           "%.3f", "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::DragFloatRange2("moisture", &b.moistMin, &b.moistMax, 0.005f, 0.0f, 1.0f,
                           "%.3f", "%.3f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::ColorEdit3("color", b.color);

    int tileCount = 0, texCount = 0;
    const char* const* tileNames = dash::editor::biomedesign::tileTypeNames(tileCount);
    const char* const* texNames = dash::editor::biomedesign::textureLayerNames(texCount);
    b.tileType = std::max(0, std::min(tileCount - 1, b.tileType));
    b.textureLayer = std::max(0, std::min(texCount - 1, b.textureLayer));
    ImGui::Combo("tile type", &b.tileType, tileNames, tileCount);
    ImGui::Combo("terrain texture", &b.textureLayer, texNames, texCount);
    ImGui::Checkbox("walkable", &b.walkable);

    ImGui::SeparatorText("Vegetation");
    int kindCount = 0;
    const char* const* kinds = dash::editor::biomedesign::vegetationKinds(kindCount);

    int removeRule = -1;
    for (int r = 0; r < static_cast<int>(b.vegetation.size()); ++r) {
        auto& rule = b.vegetation[static_cast<std::size_t>(r)];
        ImGui::PushID(1000 + r);

        int kindIdx = 0;
        for (int k = 0; k < kindCount; ++k)
            if (rule.kind == kinds[k]) { kindIdx = k; break; }
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::Combo("##kind", &kindIdx, kinds, kindCount)) rule.kind = kinds[kindIdx];
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        ImGui::SliderFloat("density", &rule.density, 0.0f, 1.0f, "%.3f");
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) removeRule = r;

        ImGui::SetNextItemWidth(160.0f);
        ImGui::DragFloatRange2("scale", &rule.minScale, &rule.maxScale, 0.01f, 0.05f, 8.0f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        ImGui::DragInt("variants", &rule.variants, 0.2f, 1, 32);

        char matBuf[160];
        std::snprintf(matBuf, sizeof(matBuf), "%s", rule.material.c_str());
        if (ImGui::InputText("material", matBuf, sizeof(matBuf))) rule.material = matBuf;
        char folBuf[160];
        std::snprintf(folBuf, sizeof(folBuf), "%s", rule.materialFoliage.c_str());
        if (ImGui::InputText("foliage material", folBuf, sizeof(folBuf))) rule.materialFoliage = folBuf;

        ImGui::Separator();
        ImGui::PopID();
    }
    if (removeRule >= 0) b.vegetation.erase(b.vegetation.begin() + removeRule);

    if (ImGui::Button("Add Vegetation Rule")) {
        dash::world::VegetationRule rule;
        rule.kind = "grass";
        rule.density = 0.05f;
        rule.minScale = 0.8f;
        rule.maxScale = 1.2f;
        rule.material = "materials/proc_grass_tuft.mat.json";
        b.vegetation.push_back(std::move(rule));
    }
}
