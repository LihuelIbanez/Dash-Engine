#include "SpriteImporter.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

ImportResult SpriteImporter::import(const std::string& sourcePath,
                                    const std::string& outputPath,
                                    AssetRecord& record)
{
    ImportResult result;

    // Validate that the source exists and is a .png
    fs::path src(sourcePath);
    if (!fs::exists(src)) {
        result.errors.push_back("Sprite file not found: " + sourcePath);
        return result;
    }
    std::string ext = src.extension().string();
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext != ".png") {
        result.errors.push_back("SpriteImporter only handles .png files: " + sourcePath);
        return result;
    }

    // Copy binary to library
    std::error_code ec;
    fs::create_directories(fs::path(outputPath).parent_path(), ec);
    if (ec) {
        result.errors.push_back("Failed to create output directory: " + ec.message());
        return result;
    }
    fs::copy_file(src, fs::path(outputPath),
                  fs::copy_options::overwrite_existing, ec);
    if (ec) {
        result.errors.push_back("Failed to copy sprite to library: " + ec.message());
        return result;
    }

    record.assetType = AssetType::Sprite;
    result.success   = true;
    return result;
}
