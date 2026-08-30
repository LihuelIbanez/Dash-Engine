#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// BiomeTable — the elevation × moisture lookup that used to be a hardcoded if
// cascade inside TerrainMesh::generate / World::generate.
//
// Pure data plus the two decisions that matter (which biome owns a sample, and
// whether the table actually covers the whole domain). No JSON, no ImGui, no
// SDL: BiomeTableFile.h adds serialization, BiomeDesignerPanel adds the UI, and
// the tests exercise this header on its own.
//
// Range convention: [min, max) on both axes, with the top edge inclusive when
// max >= 1.0 so that e == 1.0 still lands somewhere. Coverage and overlap are
// checked against that same rule, so what validate() reports is exactly what
// resolveBiome() will do.
// ─────────────────────────────────────────────────────────────────────────────

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace dash::world {

// One procedural prop family scattered over the faces of a biome. `kind` is a
// dash::procmesh model kind ("conifer", "rock", ...); the scatter turns it into
// a "proc:<kind>?seed=N" mesh id.
struct VegetationRule {
    std::string kind;
    float       density  = 0.0f;   // probability that a face of this biome grows one
    float       minScale = 1.0f;
    float       maxScale = 1.0f;
    std::string material;          // whole model, or the trunk when materialFoliage is set
    std::string materialFoliage;   // empty → no trunk/foliage split, one instance
    int         variants = 6;      // distinct seeds; bounds how many unique GPU meshes a kind costs
};

struct BiomeDef {
    std::string id;
    std::string name;
    float elevMin  = 0.0f, elevMax  = 1.0f;
    float moistMin = 0.0f, moistMax = 1.0f;
    float color[3] = {0.5f, 0.5f, 0.5f};
    int   tileType     = 3;   // TileType enum value (Grass)
    int   textureLayer = 0;   // TerrainTextureId enum value
    bool  walkable     = true;
    std::vector<VegetationRule> vegetation;
};

struct BiomeTable {
    int version = 1;
    // Hard ceiling on emitted vegetation render instances for the whole map.
    int maxVegetationInstances = 1200;
    std::vector<BiomeDef> biomes;

    bool empty() const { return biomes.empty(); }
    std::size_t size() const { return biomes.size(); }
};

// ── Lookup ───────────────────────────────────────────────────────────────────

inline bool inRange(float v, float lo, float hi)
{
    if (v < lo) return false;
    if (v < hi) return true;
    return hi >= 1.0f && v <= 1.0f;
}

inline bool biomeContains(const BiomeDef& b, float elevation, float moisture)
{
    return inRange(elevation, b.elevMin, b.elevMax) &&
           inRange(moisture, b.moistMin, b.moistMax);
}

/// Index of the first biome covering the sample, or -1 when the table has a
/// hole there. First-match order is what makes the list order meaningful.
inline int resolveBiome(const BiomeTable& table, float elevation, float moisture)
{
    for (std::size_t i = 0; i < table.biomes.size(); ++i)
        if (biomeContains(table.biomes[i], elevation, moisture))
            return static_cast<int>(i);
    return -1;
}

inline int findBiomeById(const BiomeTable& table, const std::string& id)
{
    for (std::size_t i = 0; i < table.biomes.size(); ++i)
        if (table.biomes[i].id == id) return static_cast<int>(i);
    return -1;
}

// ── Validation ───────────────────────────────────────────────────────────────

struct BiomeIssue {
    enum class Kind { InvalidRange, DuplicateId, Gap, Overlap };
    Kind        kind = Kind::Gap;
    std::string message;
    float       elevation = 0.0f;   // representative sample, for Gap / Overlap
    float       moisture  = 0.0f;
    int         biomeA = -1;
    int         biomeB = -1;
};

namespace detail {

inline std::vector<float> compressAxis(const BiomeTable& table, bool elevation)
{
    std::vector<float> edges{0.0f, 1.0f};
    for (const BiomeDef& b : table.biomes) {
        const float lo = elevation ? b.elevMin : b.moistMin;
        const float hi = elevation ? b.elevMax : b.moistMax;
        if (lo > 0.0f && lo < 1.0f) edges.push_back(lo);
        if (hi > 0.0f && hi < 1.0f) edges.push_back(hi);
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end(),
                            [](float a, float b) { return std::abs(a - b) < 1e-6f; }),
                edges.end());
    return edges;
}

inline std::string fmtRect(float e0, float e1, float m0, float m1)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "e[%.3f,%.3f) x m[%.3f,%.3f)",
                  static_cast<double>(e0), static_cast<double>(e1),
                  static_cast<double>(m0), static_cast<double>(m1));
    return buf;
}

} // namespace detail

/// Gaps and ambiguous overlaps, found exactly (not sampled) by compressing both
/// axes to the biome edges: every cell of that grid is uniform, so testing one
/// midpoint per cell decides the whole cell.
inline std::vector<BiomeIssue> validateBiomeTable(const BiomeTable& table,
                                                  std::size_t maxIssues = 32)
{
    std::vector<BiomeIssue> issues;

    for (std::size_t i = 0; i < table.biomes.size(); ++i) {
        const BiomeDef& b = table.biomes[i];
        auto bad = [&](const char* what) {
            BiomeIssue is;
            is.kind = BiomeIssue::Kind::InvalidRange;
            is.biomeA = static_cast<int>(i);
            is.message = "'" + (b.id.empty() ? std::string("<no id>") : b.id) + "': " + what;
            issues.push_back(std::move(is));
        };
        if (b.id.empty())                              bad("empty id");
        if (!(b.elevMin < b.elevMax))                  bad("elevation min >= max");
        if (!(b.moistMin < b.moistMax))                bad("moisture min >= max");
        if (b.elevMin < 0.0f || b.elevMax > 1.0f)      bad("elevation outside [0,1]");
        if (b.moistMin < 0.0f || b.moistMax > 1.0f)    bad("moisture outside [0,1]");

        for (std::size_t j = i + 1; j < table.biomes.size(); ++j) {
            if (!b.id.empty() && b.id == table.biomes[j].id) {
                BiomeIssue is;
                is.kind = BiomeIssue::Kind::DuplicateId;
                is.biomeA = static_cast<int>(i);
                is.biomeB = static_cast<int>(j);
                is.message = "duplicate id '" + b.id + "'";
                issues.push_back(std::move(is));
            }
        }
    }

    const std::vector<float> eEdges = detail::compressAxis(table, true);
    const std::vector<float> mEdges = detail::compressAxis(table, false);

    for (std::size_t ei = 0; ei + 1 < eEdges.size() && issues.size() < maxIssues; ++ei) {
        for (std::size_t mi = 0; mi + 1 < mEdges.size() && issues.size() < maxIssues; ++mi) {
            const float e = (eEdges[ei] + eEdges[ei + 1]) * 0.5f;
            const float m = (mEdges[mi] + mEdges[mi + 1]) * 0.5f;

            int first = -1, second = -1;
            for (std::size_t b = 0; b < table.biomes.size(); ++b) {
                if (!biomeContains(table.biomes[b], e, m)) continue;
                if (first < 0)       first = static_cast<int>(b);
                else if (second < 0) second = static_cast<int>(b);
            }

            const std::string rect = detail::fmtRect(eEdges[ei], eEdges[ei + 1],
                                                     mEdges[mi], mEdges[mi + 1]);
            if (first < 0) {
                BiomeIssue is;
                is.kind = BiomeIssue::Kind::Gap;
                is.elevation = e;
                is.moisture = m;
                is.message = "gap: no biome covers " + rect;
                issues.push_back(std::move(is));
            } else if (second >= 0) {
                BiomeIssue is;
                is.kind = BiomeIssue::Kind::Overlap;
                is.elevation = e;
                is.moisture = m;
                is.biomeA = first;
                is.biomeB = second;
                is.message = "overlap: '" + table.biomes[static_cast<std::size_t>(first)].id +
                             "' and '" + table.biomes[static_cast<std::size_t>(second)].id +
                             "' both cover " + rect + " (first wins)";
                issues.push_back(std::move(is));
            }
        }
    }

    return issues;
}

inline bool hasCoverageGap(const BiomeTable& table)
{
    for (const BiomeIssue& i : validateBiomeTable(table))
        if (i.kind == BiomeIssue::Kind::Gap) return true;
    return false;
}

} // namespace dash::world
