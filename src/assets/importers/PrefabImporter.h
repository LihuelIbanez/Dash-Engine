#pragma once
#include "IImporter.h"

// ─────────────────────────────────────────────────────────────────────────────
// PrefabImporter – validates and copies prefab JSON files to the library.
// ─────────────────────────────────────────────────────────────────────────────
class PrefabImporter : public IImporter {
public:
    ImportResult import(const std::string& sourcePath,
                        const std::string& outputPath,
                        AssetRecord& record) override;

    AssetType assetType() const override { return AssetType::Prefab; }
};
