#pragma once
#include "IImporter.h"

class TileSetImporter : public IImporter {
public:
    ImportResult import(const std::string& sourcePath,
                        const std::string& outputPath,
                        AssetRecord& record) override;
    AssetType assetType() const override { return AssetType::TileSet; }
};
