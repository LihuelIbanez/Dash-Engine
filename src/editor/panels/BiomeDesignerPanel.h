#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// BiomeDesignerPanel — author assets/world/biomes.json against a live map.
//
// Everything that decides something (the preview raster, its run-length
// compression, the reorder/add/remove edits and the palette of procedural
// vegetation kinds) lives in dash::editor::biomedesign: header-only and
// ImGui-free, so tests/test_biome_designer.cpp can exercise it headless.
// ─────────────────────────────────────────────────────────────────────────────

#include "world/BiomeTable.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace dash::editor::biomedesign {

using dash::world::BiomeDef;
using dash::world::BiomeTable;

// ── Preview raster ───────────────────────────────────────────────────────────

struct PreviewMap {
    int width = 0, height = 0;
    std::vector<int16_t> biome;   // width*height, -1 = uncovered
};

/// Re-maps the cached elevation/moisture of the terrain to the *edited* table,
/// so moving a range updates the map without re-running the noise.
inline PreviewMap buildPreview(const BiomeTable& table,
                               const std::vector<float>& elevation,
                               const std::vector<float>& moisture,
                               int faceW, int faceH,
                               int previewSize)
{
    PreviewMap out;
    if (faceW <= 0 || faceH <= 0 || previewSize <= 0) return out;
    const std::size_t faces = static_cast<std::size_t>(faceW) * static_cast<std::size_t>(faceH);
    if (elevation.size() < faces || moisture.size() < faces) return out;

    out.width = std::min(previewSize, faceW);
    out.height = std::min(previewSize, faceH);
    out.biome.assign(static_cast<std::size_t>(out.width) * out.height, -1);

    for (int py = 0; py < out.height; ++py) {
        const int fy = py * faceH / out.height;
        for (int px = 0; px < out.width; ++px) {
            const int fx = px * faceW / out.width;
            const std::size_t src = static_cast<std::size_t>(fy) * faceW + fx;
            out.biome[static_cast<std::size_t>(py) * out.width + px] =
                static_cast<int16_t>(dash::world::resolveBiome(table, elevation[src], moisture[src]));
        }
    }
    return out;
}

/// Horizontal runs of equal biome, so the panel emits tens of rectangles per row
/// instead of one per pixel. A 256-wide preview drawn cell by cell would be 65k
/// draw commands every frame.
struct PreviewRun {
    int y = 0, x0 = 0, x1 = 0;   // [x0, x1)
    int16_t biome = -1;
};

inline std::vector<PreviewRun> compressPreview(const PreviewMap& map)
{
    std::vector<PreviewRun> runs;
    if (map.width <= 0 || map.height <= 0) return runs;
    for (int y = 0; y < map.height; ++y) {
        int x = 0;
        while (x < map.width) {
            const int16_t b = map.biome[static_cast<std::size_t>(y) * map.width + x];
            int end = x + 1;
            while (end < map.width &&
                   map.biome[static_cast<std::size_t>(y) * map.width + end] == b)
                ++end;
            runs.push_back({y, x, end, b});
            x = end;
        }
    }
    return runs;
}

inline std::vector<int> previewHistogram(const PreviewMap& map, std::size_t biomeCount)
{
    std::vector<int> counts(biomeCount + 1, 0);   // last slot = uncovered
    for (int16_t b : map.biome) {
        if (b < 0 || b >= static_cast<int16_t>(biomeCount)) ++counts.back();
        else ++counts[static_cast<std::size_t>(b)];
    }
    return counts;
}

// ── List edits ───────────────────────────────────────────────────────────────

inline bool moveBiome(BiomeTable& table, int index, int delta)
{
    const int n = static_cast<int>(table.biomes.size());
    const int dst = index + delta;
    if (index < 0 || index >= n || dst < 0 || dst >= n) return false;
    std::swap(table.biomes[static_cast<std::size_t>(index)],
              table.biomes[static_cast<std::size_t>(dst)]);
    return true;
}

inline bool removeBiome(BiomeTable& table, int index)
{
    if (index < 0 || index >= static_cast<int>(table.biomes.size())) return false;
    table.biomes.erase(table.biomes.begin() + index);
    return true;
}

/// Ids are the identity used by scenes and by findBiomeById, so a new entry gets
/// a free one instead of a duplicate the validator would immediately flag.
inline std::string uniqueBiomeId(const BiomeTable& table, const std::string& base)
{
    if (dash::world::findBiomeById(table, base) < 0) return base;
    for (int i = 2; i < 1000; ++i) {
        const std::string candidate = base + "_" + std::to_string(i);
        if (dash::world::findBiomeById(table, candidate) < 0) return candidate;
    }
    return base + "_new";
}

inline int addBiome(BiomeTable& table, const std::string& baseId = "new_biome")
{
    BiomeDef b;
    b.id = uniqueBiomeId(table, baseId);
    b.name = "New Biome";
    b.elevMin = 0.4f; b.elevMax = 0.5f;
    b.moistMin = 0.4f; b.moistMax = 0.5f;
    b.color[0] = 0.8f; b.color[1] = 0.4f; b.color[2] = 0.8f;
    table.biomes.push_back(std::move(b));
    return static_cast<int>(table.biomes.size()) - 1;
}

// ── Palettes shared with the UI ──────────────────────────────────────────────

inline const char* const* vegetationKinds(int& count)
{
    static const char* kKinds[] = { "conifer", "broadleaf", "rock", "bush", "grass", "stump", "log" };
    count = 7;
    return kKinds;
}

inline const char* const* tileTypeNames(int& count)
{
    static const char* kNames[] = { "DeepWater", "Water", "Sand", "Grass",
                                    "Forest", "Dirt", "Stone", "Mountain", "Snow" };
    count = 9;
    return kNames;
}

inline const char* const* textureLayerNames(int& count)
{
    static const char* kNames[] = { "Grass", "Dirt", "Rock", "Sand", "Snow",
                                    "Mud", "DarkGrass", "Gravel", "Ice" };
    count = 9;
    return kNames;
}

} // namespace dash::editor::biomedesign

// ── ImGui panel ──────────────────────────────────────────────────────────────
#ifndef DASH_BIOME_DESIGNER_NO_IMGUI

#include "imgui.h"

#include <functional>

class TerrainMesh;

class BiomeDesignerPanel {
public:
    using LogCallback = std::function<void(const std::string&)>;
    using RegenerateCallback = std::function<void(unsigned int)>;

    // `table` is the live table the editor generates from; regenerate re-runs
    // the world with the given seed after the table has been edited.
    // `biomeTableId` is the active scene's SceneData::biomeTableId: empty means
    // the default assets/world/biomes.json; "Save As..." writes a named table
    // under assets/world/biomes/ and assigns it to the scene.
    void draw(dash::world::BiomeTable& table,
              const TerrainMesh& terrain,
              const std::string& assetsRoot,
              unsigned int& worldSeed,
              std::string& biomeTableId,
              const RegenerateCallback& regenerate,
              const LogCallback& log);

private:
    void drawPreview(const dash::world::BiomeTable& table, const TerrainMesh& terrain);
    void drawBiomeEditor(dash::world::BiomeTable& table);
    void drawIssues(const dash::world::BiomeTable& table);

    int   selected_ = 0;
    int   previewSize_ = 128;
    bool  previewDirty_ = true;
    dash::editor::biomedesign::PreviewMap preview_;
    std::vector<dash::editor::biomedesign::PreviewRun> previewRuns_;
    std::vector<int> previewCounts_;
    unsigned int previewGeneration_ = 0;
    char saveAsNameBuf_[96] = {0};
};

#endif // DASH_BIOME_DESIGNER_NO_IMGUI
