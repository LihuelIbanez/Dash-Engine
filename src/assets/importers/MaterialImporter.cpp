#include "MaterialImporter.h"
#include "MaterialAsset.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

ImportResult MaterialImporter::import(const std::string& sourcePath,
                                      const std::string& outputPath,
                                      AssetRecord& record)
{
    ImportResult result;

    std::ifstream in(sourcePath);
    if (!in.is_open()) {
        result.errors.push_back("Cannot open material file: " + sourcePath);
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
        result.errors.push_back("Material root is not a JSON object.");
        return result;
    }

    MaterialAsset mat = MaterialAsset::fromJson(root);

    // The database owns the GUID; the imported copy carries it so the renderer
    // can round-trip a material back to its record.
    if (!record.guid.empty())
        mat.guid = record.guid;

    if (!mat.albedoTexture.empty()) {
        const fs::path tex = fs::path(sourcePath).parent_path() / mat.albedoTexture;
        std::error_code ec;
        if (!fs::exists(tex, ec) && !fs::exists(fs::path(mat.albedoTexture), ec))
            result.errors.push_back("albedoTexture not found: " + mat.albedoTexture);
    }

    fs::create_directories(fs::path(outputPath).parent_path());
    if (!mat.saveToFile(outputPath)) {
        result.errors.push_back("Cannot write to: " + outputPath);
        return result;
    }

    record.assetType = AssetType::Material;
    result.success = true;
    return result;
}
