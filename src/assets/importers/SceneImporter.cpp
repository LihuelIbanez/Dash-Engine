#include "SceneImporter.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

ImportResult SceneImporter::import(const std::string& sourcePath,
                                   const std::string& outputPath,
                                   AssetRecord& record)
{
    ImportResult result;

    // Validate: must be valid JSON with expected scene fields
    std::ifstream in(sourcePath);
    if (!in.is_open()) {
        result.errors.push_back("Cannot open scene file: " + sourcePath);
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

    if (!root.is_object()) {
        result.errors.push_back("Scene root is not a JSON object.");
        return result;
    }

    // Copy validated scene to library output
    fs::create_directories(fs::path(outputPath).parent_path());
    std::ofstream out(outputPath);
    if (!out.is_open()) {
        result.errors.push_back("Cannot write to: " + outputPath);
        return result;
    }
    out << root.dump(2) << '\n';
    out.close();

    record.assetType = AssetType::Scene;
    result.success = true;
    return result;
}
