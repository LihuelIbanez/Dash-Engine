#include "GameplayConfigImporter.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

ImportResult GameplayConfigImporter::import(const std::string& sourcePath,
                                            const std::string& outputPath,
                                            AssetRecord& record)
{
    ImportResult result;

    // Validate: must be valid JSON
    std::ifstream in(sourcePath);
    if (!in.is_open()) {
        result.errors.push_back("Cannot open config file: " + sourcePath);
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

    if (!root.is_object() && !root.is_array()) {
        result.errors.push_back("GameplayConfig root is not a JSON object or array.");
        return result;
    }

    // Copy validated config to library
    fs::create_directories(fs::path(outputPath).parent_path());
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        result.errors.push_back("Cannot write to: " + outputPath);
        return result;
    }
    out << root.dump(2) << '\n';
    out.close();

    record.assetType = AssetType::GameplayConfig;
    result.success = true;
    return result;
}
