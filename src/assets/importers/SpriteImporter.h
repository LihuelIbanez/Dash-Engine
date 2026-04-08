#pragma once
#include "IImporter.h"

// ─────────────────────────────────────────────────────────────────────────────
// SpriteImporter — copies PNG sprite files to library/sprites/<guid>.png
//                  and registers them in AssetDatabase as AssetType::Sprite.
// ─────────────────────────────────────────────────────────────────────────────
class SpriteImporter : public IImporter {
public:
    ImportResult import(const std::string& sourcePath,
                        const std::string& outputPath,
                        AssetRecord& record) override;

    AssetType assetType() const override { return AssetType::Sprite; }
};
