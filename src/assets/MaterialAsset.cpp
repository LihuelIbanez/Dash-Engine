#include "MaterialAsset.h"

#include <algorithm>
#include <fstream>

using json = nlohmann::json;

nlohmann::json MaterialAsset::toJson() const
{
    json j;
    j["guid"] = guid;
    j["name"] = name;
    j["albedoTexture"] = albedoTexture;
    j["baseColor"] = {baseColor[0], baseColor[1], baseColor[2]};
    j["metallic"] = metallic;
    j["roughness"] = roughness;
    return j;
}

MaterialAsset MaterialAsset::fromJson(const nlohmann::json& j)
{
    MaterialAsset m;
    if (!j.is_object()) return m;

    m.guid = j.value("guid", std::string{});
    m.name = j.value("name", std::string("default"));
    m.albedoTexture = j.value("albedoTexture", std::string{});

    if (j.contains("baseColor") && j["baseColor"].is_array() && j["baseColor"].size() == 3) {
        for (int i = 0; i < 3; ++i) {
            if (j["baseColor"][i].is_number())
                m.baseColor[i] = j["baseColor"][i].get<float>();
        }
    }

    // Absent in materials authored before the PBR switch: keep the defaults.
    if (j.contains("metallic") && j["metallic"].is_number())
        m.metallic = std::clamp(j["metallic"].get<float>(), 0.0f, 1.0f);
    if (j.contains("roughness") && j["roughness"].is_number())
        m.roughness = std::clamp(j["roughness"].get<float>(), 0.0f, 1.0f);

    return m;
}

bool MaterialAsset::saveToFile(const std::string& path) const
{
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << toJson().dump(2);
    return true;
}

bool MaterialAsset::loadFromFile(const std::string& path)
{
    std::ifstream in(path);
    if (!in.is_open()) return false;

    json j;
    try {
        in >> j;
    } catch (...) {
        return false;
    }

    *this = fromJson(j);
    return true;
}
