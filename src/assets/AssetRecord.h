#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "AssetTypes.h"

struct AssetRecord {
    std::string guid;                  // stable unique identifier (UUID v4)
    std::string sourcePath;            // relative path inside assets/
    std::string importPath;            // relative path inside library/
    AssetType   assetType = AssetType::Unknown;
    std::string hash;                  // content hash of source file
    int64_t     lastImportTime = 0;    // unix timestamp of last import
    std::vector<std::string> dependencies; // GUIDs of assets this depends on
};
