#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// BiomeTableFile — read/write assets/world/biomes.json.
//
// Split from BiomeTable.h for the same reason AnimationStateMachineFile.h is
// split from AnimationStateMachine.h: the lookup and the validation must stay
// usable from translation units that do not want nlohmann pulled in.
//
// Schema (version 1):
// {
//   "version": 1,
//   "maxVegetationInstances": 1200,
//   "biomes": [
//     { "id": "forest", "name": "Lowland Forest",
//       "elevation": [0.37, 0.60], "moisture": [0.60, 1.0],
//       "color": [0.12, 0.29, 0.10],
//       "tileType": 4, "textureLayer": 6, "walkable": true,
//       "vegetation": [
//         { "kind": "conifer", "density": 0.22, "scale": [0.9, 1.35],
//           "material": "materials/proc_bark.mat.json",
//           "materialFoliage": "materials/proc_conifer_needles.mat.json",
//           "variants": 6 }
//       ] }
//   ]
// }
// ─────────────────────────────────────────────────────────────────────────────

#include "BiomeTable.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace dash::world {

inline constexpr int kBiomeTableVersion = 1;

namespace detail {

inline bool readPair(const nlohmann::json& j, const char* key, float& lo, float& hi)
{
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 2) return false;
    if (!(*it)[0].is_number() || !(*it)[1].is_number()) return false;
    lo = (*it)[0].get<float>();
    hi = (*it)[1].get<float>();
    return true;
}

} // namespace detail

inline bool parseBiomeTable(const nlohmann::json& j, BiomeTable& out, std::string* error)
{
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };

    if (!j.is_object()) return fail("root is not an object");

    BiomeTable table;
    table.version = j.value("version", kBiomeTableVersion);
    if (table.version > kBiomeTableVersion)
        return fail("version " + std::to_string(table.version) + " is newer than " +
                    std::to_string(kBiomeTableVersion));
    table.maxVegetationInstances = j.value("maxVegetationInstances", 1200);

    auto biomes = j.find("biomes");
    if (biomes == j.end() || !biomes->is_array() || biomes->empty())
        return fail("missing or empty 'biomes' array");

    for (const auto& jb : *biomes) {
        if (!jb.is_object()) return fail("a biome entry is not an object");

        BiomeDef b;
        b.id = jb.value("id", std::string());
        if (b.id.empty()) return fail("a biome entry has no 'id'");
        b.name = jb.value("name", b.id);

        if (!detail::readPair(jb, "elevation", b.elevMin, b.elevMax))
            return fail("'" + b.id + "': 'elevation' must be [min, max]");
        if (!detail::readPair(jb, "moisture", b.moistMin, b.moistMax))
            return fail("'" + b.id + "': 'moisture' must be [min, max]");

        auto jc = jb.find("color");
        if (jc != jb.end() && jc->is_array() && jc->size() == 3)
            for (int i = 0; i < 3; ++i) b.color[i] = (*jc)[static_cast<std::size_t>(i)].get<float>();

        b.tileType     = jb.value("tileType", 3);
        b.textureLayer = jb.value("textureLayer", 0);
        b.walkable     = jb.value("walkable", true);

        auto jv = jb.find("vegetation");
        if (jv != jb.end() && jv->is_array()) {
            for (const auto& jr : *jv) {
                if (!jr.is_object()) return fail("'" + b.id + "': a vegetation entry is not an object");
                VegetationRule r;
                r.kind = jr.value("kind", std::string());
                if (r.kind.empty()) return fail("'" + b.id + "': a vegetation entry has no 'kind'");
                r.density = jr.value("density", 0.0f);
                float lo = 1.0f, hi = 1.0f;
                if (detail::readPair(jr, "scale", lo, hi)) { r.minScale = lo; r.maxScale = hi; }
                r.material        = jr.value("material", std::string());
                r.materialFoliage = jr.value("materialFoliage", std::string());
                r.variants        = jr.value("variants", 6);
                b.vegetation.push_back(std::move(r));
            }
        }

        table.biomes.push_back(std::move(b));
    }

    out = std::move(table);
    return true;
}

inline nlohmann::json biomeTableToJson(const BiomeTable& table)
{
    nlohmann::json j;
    j["version"] = kBiomeTableVersion;
    j["maxVegetationInstances"] = table.maxVegetationInstances;
    j["biomes"] = nlohmann::json::array();

    for (const BiomeDef& b : table.biomes) {
        nlohmann::json jb;
        jb["id"] = b.id;
        jb["name"] = b.name;
        jb["elevation"] = { b.elevMin, b.elevMax };
        jb["moisture"] = { b.moistMin, b.moistMax };
        jb["color"] = { b.color[0], b.color[1], b.color[2] };
        jb["tileType"] = b.tileType;
        jb["textureLayer"] = b.textureLayer;
        jb["walkable"] = b.walkable;
        jb["vegetation"] = nlohmann::json::array();
        for (const VegetationRule& r : b.vegetation) {
            nlohmann::json jr;
            jr["kind"] = r.kind;
            jr["density"] = r.density;
            jr["scale"] = { r.minScale, r.maxScale };
            jr["material"] = r.material;
            if (!r.materialFoliage.empty()) jr["materialFoliage"] = r.materialFoliage;
            jr["variants"] = r.variants;
            jb["vegetation"].push_back(std::move(jr));
        }
        j["biomes"].push_back(std::move(jb));
    }
    return j;
}

inline std::string writeBiomeTableString(const BiomeTable& table)
{
    return biomeTableToJson(table).dump(2) + "\n";
}

inline bool parseBiomeTableString(const std::string& text, BiomeTable& out, std::string* error)
{
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) {
        if (error) *error = "malformed JSON";
        return false;
    }
    return parseBiomeTable(j, out, error);
}

inline bool loadBiomeTableFile(const std::string& path, BiomeTable& out, std::string* error)
{
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseBiomeTableString(ss.str(), out, error);
}

inline bool saveBiomeTableFile(const std::string& path, const BiomeTable& table, std::string* error)
{
    std::ofstream os(path, std::ios::trunc);
    if (!os) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    os << writeBiomeTableString(table);
    if (!os) {
        if (error) *error = "write failed for " + path;
        return false;
    }
    return true;
}

inline std::string biomeTablePath(const std::string& assetsRoot)
{
    if (assetsRoot.empty()) return "world/biomes.json";
    const char tail = assetsRoot.back();
    return assetsRoot + ((tail == '/' || tail == '\\') ? "" : "/") + "world/biomes.json";
}

} // namespace dash::world
