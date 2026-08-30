#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// BiomeScatter — turns a per-face biome map into procedural vegetation.
//
// Pure and deterministic: same (table, biome map, seed) always yields the same
// list, so the editor viewport and the runtime grow the identical forest.
//
// Two things keep 256×256 faces from exploding into a draw-call storm:
//   * one plant per face at most, and
//   * a global instance cap applied as a uniform thinning pass — the first pass
//     counts what the densities would produce, the second keeps a deterministic
//     fraction of it. Truncating row-major instead would pile the whole forest
//     into the north edge of the map.
// Seeds come from a small per-rule palette (VegetationRule::variants) because
// the mesh id is the GPU cache key: a unique seed per plant would mean one
// unique mesh per plant.
// ─────────────────────────────────────────────────────────────────────────────

#include "BiomeTable.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace dash::world {

struct ScatterInstance {
    float x = 0.0f, y = 0.0f;   // tile-space position (same space as TerrainMesh::sampleHeight)
    float scale = 1.0f;
    float yawDeg = 0.0f;
    std::string meshId;
    std::string materialId;
    std::string kind;
    int biome = -1;
};

struct ScatterStats {
    int  faces = 0;          // faces with a biome that has at least one rule
    int  wanted = 0;         // plants the densities asked for, before the cap
    int  plants = 0;         // plants actually placed
    int  instances = 0;      // render instances emitted (a split plant emits two)
    bool capped = false;
    std::vector<std::pair<std::string, int>> perKind;   // plants per kind, sorted by kind
};

namespace detail {

inline uint32_t scatterHash(uint32_t seed, int fx, int fy, uint32_t salt)
{
    uint32_t h = seed * 0x9E3779B9u;
    h ^= static_cast<uint32_t>(fx) * 0x85EBCA6Bu;
    h = (h << 13) | (h >> 19);
    h ^= static_cast<uint32_t>(fy) * 0xC2B2AE35u;
    h = (h << 7) | (h >> 25);
    h ^= salt * 0x27D4EB2Fu;
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    return h;
}

inline float unit(uint32_t h) { return static_cast<float>(h & 0x00FFFFFFu) / 16777216.0f; }

inline std::string procMeshId(const std::string& kind, uint32_t seed, const char* part)
{
    std::string id = "proc:" + kind + "?seed=" + std::to_string(seed);
    if (part) id += "&part=" + std::string(part);
    return id;
}

} // namespace detail

/// `faceBiome` is FW*FH row-major; -1 marks a face no biome owns or one the
/// terrain post-pass overwrote (coastline sand, filled ponds), which must stay
/// bare.
inline std::vector<ScatterInstance> scatterVegetation(const BiomeTable& table,
                                                      const std::vector<int16_t>& faceBiome,
                                                      int fw, int fh,
                                                      uint32_t seed,
                                                      int maxInstances,
                                                      ScatterStats* stats = nullptr)
{
    std::vector<ScatterInstance> out;
    if (stats) *stats = ScatterStats{};
    if (table.empty() || fw <= 0 || fh <= 0 || maxInstances <= 0) return out;
    if (faceBiome.size() < static_cast<std::size_t>(fw) * static_cast<std::size_t>(fh)) return out;

    // A rule that splits into trunk + foliage costs two instances.
    auto instanceCost = [](const VegetationRule& r) { return r.materialFoliage.empty() ? 1 : 2; };

    // Pass 1 — what the densities want, and what that would cost.
    int wantedPlants = 0;
    long long wantedInstances = 0;
    int biomeFaces = 0;
    for (int fy = 0; fy < fh; ++fy) {
        for (int fx = 0; fx < fw; ++fx) {
            const int b = faceBiome[static_cast<std::size_t>(fy) * fw + fx];
            if (b < 0 || b >= static_cast<int>(table.biomes.size())) continue;
            const BiomeDef& def = table.biomes[static_cast<std::size_t>(b)];
            if (def.vegetation.empty()) continue;
            ++biomeFaces;
            for (std::size_t r = 0; r < def.vegetation.size(); ++r) {
                const VegetationRule& rule = def.vegetation[r];
                if (rule.density <= 0.0f || rule.kind.empty()) continue;
                const float roll = detail::unit(
                    detail::scatterHash(seed, fx, fy, static_cast<uint32_t>(r) + 1u));
                if (roll >= rule.density) continue;
                ++wantedPlants;
                wantedInstances += instanceCost(rule);
                break;   // one plant per face
            }
        }
    }

    const bool capped = wantedInstances > maxInstances;
    const float keepRatio = capped
        ? static_cast<float>(maxInstances) / static_cast<float>(wantedInstances)
        : 1.0f;

    // Pass 2 — emit, thinning uniformly when the cap bites.
    out.reserve(static_cast<std::size_t>(capped ? maxInstances : wantedInstances));
    std::vector<std::pair<std::string, int>> perKind;
    int plants = 0, instances = 0;

    for (int fy = 0; fy < fh; ++fy) {
        for (int fx = 0; fx < fw; ++fx) {
            const int b = faceBiome[static_cast<std::size_t>(fy) * fw + fx];
            if (b < 0 || b >= static_cast<int>(table.biomes.size())) continue;
            const BiomeDef& def = table.biomes[static_cast<std::size_t>(b)];

            for (std::size_t r = 0; r < def.vegetation.size(); ++r) {
                const VegetationRule& rule = def.vegetation[r];
                if (rule.density <= 0.0f || rule.kind.empty()) continue;
                const float roll = detail::unit(
                    detail::scatterHash(seed, fx, fy, static_cast<uint32_t>(r) + 1u));
                if (roll >= rule.density) continue;

                if (capped) {
                    const float keep = detail::unit(detail::scatterHash(seed, fx, fy, 0xB10Eu));
                    if (keep >= keepRatio) break;
                }
                if (instances + instanceCost(rule) > maxInstances) break;

                const uint32_t hPos   = detail::scatterHash(seed, fx, fy, 0x51u);
                const uint32_t hYaw   = detail::scatterHash(seed, fx, fy, 0x52u);
                const uint32_t hScale = detail::scatterHash(seed, fx, fy, 0x53u);
                const uint32_t hSeed  = detail::scatterHash(seed, fx, fy, 0x54u);

                const int variants = rule.variants > 0 ? rule.variants : 1;
                const uint32_t meshSeed = 1u + (hSeed % static_cast<uint32_t>(variants));
                const float lo = rule.minScale > 0.0f ? rule.minScale : 1.0f;
                const float hi = rule.maxScale > lo ? rule.maxScale : lo;

                ScatterInstance inst;
                inst.x = static_cast<float>(fx) + 0.15f + 0.70f * detail::unit(hPos);
                inst.y = static_cast<float>(fy) + 0.15f + 0.70f * detail::unit(hPos >> 8);
                inst.scale = lo + (hi - lo) * detail::unit(hScale);
                inst.yawDeg = 360.0f * detail::unit(hYaw);
                inst.kind = rule.kind;
                inst.biome = b;

                if (rule.materialFoliage.empty()) {
                    inst.meshId = detail::procMeshId(rule.kind, meshSeed, nullptr);
                    inst.materialId = rule.material;
                    out.push_back(inst);
                    ++instances;
                } else {
                    ScatterInstance trunk = inst;
                    trunk.meshId = detail::procMeshId(rule.kind, meshSeed, "trunk");
                    trunk.materialId = rule.material;
                    out.push_back(std::move(trunk));

                    ScatterInstance foliage = inst;
                    foliage.meshId = detail::procMeshId(rule.kind, meshSeed, "foliage");
                    foliage.materialId = rule.materialFoliage;
                    out.push_back(std::move(foliage));
                    instances += 2;
                }

                ++plants;
                bool counted = false;
                for (auto& kv : perKind) {
                    if (kv.first == rule.kind) { ++kv.second; counted = true; break; }
                }
                if (!counted) perKind.emplace_back(rule.kind, 1);
                break;
            }
        }
    }

    if (stats) {
        stats->faces = biomeFaces;
        stats->wanted = wantedPlants;
        stats->plants = plants;
        stats->instances = instances;
        stats->capped = capped;
        std::sort(perKind.begin(), perKind.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        stats->perKind = std::move(perKind);
    }
    return out;
}

} // namespace dash::world
