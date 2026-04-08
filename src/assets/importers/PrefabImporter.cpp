#include "PrefabImporter.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

ImportResult PrefabImporter::import(const std::string& sourcePath,
                                    const std::string& outputPath,
                                    AssetRecord& record)
{
    ImportResult result;

    std::ifstream in(sourcePath);
    if (!in.is_open()) {
        result.errors.push_back("Cannot open prefab file: " + sourcePath);
        return result;
    }

    json root;
    try {
        root = json::parse(in);
    } catch (const json::parse_error& e) {
        result.errors.push_back("JSON parse error: " + std::string(e.what()));
        return result;
    }
    in.close();

    // Validate required fields.
    if (!root.contains("guid") || !root["guid"].is_string()) {
        result.errors.push_back("Prefab missing required 'guid' field: " + sourcePath);
        return result;
    }
    if (!root.contains("name") || !root["name"].is_string()) {
        result.errors.push_back("Prefab missing required 'name' field: " + sourcePath);
        return result;
    }
    if (!root.contains("components") || !root["components"].is_array()) {
        result.errors.push_back("Prefab missing 'components' array: " + sourcePath);
        return result;
    }

    // Copy to library.
    fs::create_directories(fs::path(outputPath).parent_path());
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        result.errors.push_back("Cannot write to: " + outputPath);
        return result;
    }
    out << root.dump(2) << '\n';
    out.close();

    record.assetType = AssetType::Prefab;
    result.success   = true;
    return result;
}
