#include "PrefabAsset.h"
#include "ComponentSerialization.h"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
PrefabAsset loadPrefab(const std::string& path)
{
    PrefabAsset result;
    std::ifstream file(path);
    if (!file.is_open()) return result;

    json j;
    try { j = json::parse(file); }
    catch (...) { return result; }

    if (!j.is_object() || !j.contains("guid") || !j.contains("name"))
        return result;

    result.guid = j.value("guid", "");
    result.name = j.value("name", "");

    if (j.contains("components") && j["components"].is_array()) {
        for (auto& cj : j["components"]) {
            try {
                result.defaultComponents.push_back(componentFromJson(cj));
            } catch (...) {}
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
bool savePrefab(const PrefabAsset& prefab, const std::string& path)
{
    json j;
    j["guid"] = prefab.guid;
    j["name"] = prefab.name;

    json compsArr = json::array();
    for (auto& c : prefab.defaultComponents)
        compsArr.push_back(componentToJson(c));
    j["components"] = compsArr;

    fs::create_directories(fs::path(path).parent_path());
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << j.dump(2) << '\n';
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
std::vector<ComponentVariant> instantiate(const PrefabAsset& prefab)
{
    // ComponentVariant holds only value types → copy is a deep copy.
    return prefab.defaultComponents;
}

// ─────────────────────────────────────────────────────────────────────────────
nlohmann::json computeOverrides(const PrefabAsset& prefab,
                                const std::vector<ComponentVariant>& instance)
{
    json overrides;
    overrides["modified"] = json::object();
    overrides["added"]    = json::array();
    overrides["removed"]  = json::array();

    // Check each instance component against the prefab baseline.
    for (const auto& instComp : instance) {
        json instJ = componentToJson(instComp);
        std::string typeName = instJ.value("type", "");

        bool foundInPrefab = false;
        for (const auto& prefabComp : prefab.defaultComponents) {
            json prefabJ = componentToJson(prefabComp);
            if (prefabJ.value("type", "") == typeName) {
                foundInPrefab = true;
                // Compute field-level diff.
                json diff = json::object();
                for (auto& [key, val] : instJ.items()) {
                    if (key == "type") continue;
                    if (!prefabJ.contains(key) || prefabJ[key] != val)
                        diff[key] = val;
                }
                if (!diff.empty())
                    overrides["modified"][typeName] = diff;
                break;
            }
        }
        if (!foundInPrefab)
            overrides["added"].push_back(instJ);
    }

    // Components in the prefab that are absent from the instance → removed.
    for (const auto& prefabComp : prefab.defaultComponents) {
        json prefabJ = componentToJson(prefabComp);
        std::string typeName = prefabJ.value("type", "");

        bool foundInInst = false;
        for (const auto& instComp : instance) {
            json instJ = componentToJson(instComp);
            if (instJ.value("type", "") == typeName) { foundInInst = true; break; }
        }
        if (!foundInInst)
            overrides["removed"].push_back(typeName);
    }

    return overrides;
}

// ─────────────────────────────────────────────────────────────────────────────
void applyOverrides(const PrefabAsset& prefab,
                    std::vector<ComponentVariant>& instance,
                    const nlohmann::json& overrides)
{
    // Start from a clean copy of defaults.
    instance = instantiate(prefab);

    // Apply removals.
    if (overrides.contains("removed") && overrides["removed"].is_array()) {
        for (const auto& rem : overrides["removed"]) {
            std::string typeName = rem.get<std::string>();
            instance.erase(
                std::remove_if(instance.begin(), instance.end(),
                    [&typeName](const ComponentVariant& c) {
                        return componentToJson(c).value("type", "") == typeName;
                    }),
                instance.end());
        }
    }

    // Apply field-level modifications.
    if (overrides.contains("modified") && overrides["modified"].is_object()) {
        for (auto& [typeName, diff] : overrides["modified"].items()) {
            for (auto& comp : instance) {
                json cj = componentToJson(comp);
                if (cj.value("type", "") == typeName) {
                    for (auto& [key, val] : diff.items())
                        cj[key] = val;
                    try { comp = componentFromJson(cj); } catch (...) {}
                    break;
                }
            }
        }
    }

    // Apply additions (components not present in the prefab base).
    if (overrides.contains("added") && overrides["added"].is_array()) {
        for (const auto& addJ : overrides["added"]) {
            try { instance.push_back(componentFromJson(addJ)); } catch (...) {}
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
PrefabAsset findPrefabByGuid(const std::string& prefabsDir,
                              const std::string& guid)
{
    if (guid.empty() || !fs::is_directory(prefabsDir))
        return {};

    for (auto& entry : fs::directory_iterator(
            prefabsDir, fs::directory_options::skip_permission_denied)) {
        if (entry.path().extension() != ".json") continue;
        PrefabAsset p = loadPrefab(entry.path().string());
        if (p.guid == guid) return p;
    }
    return {};
}
