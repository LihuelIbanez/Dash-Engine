#pragma once
#include "AssetRecord.h"
#include <string>
#include <vector>

struct ImportResult {
    bool success = false;
    std::vector<std::string> errors;
    std::vector<std::string> dependencyGuids;  // GUIDs this asset depends on
};

class IImporter {
public:
    virtual ~IImporter() = default;

    // Import a source asset to the output path, updating metadata in record.
    virtual ImportResult import(const std::string& sourcePath,
                                const std::string& outputPath,
                                AssetRecord& record) = 0;

    // Which asset type this importer handles.
    virtual AssetType assetType() const = 0;
};
