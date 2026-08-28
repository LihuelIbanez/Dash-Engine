#pragma once
#include "IImporter.h"

class AudioImporter : public IImporter {
public:
    ImportResult import(const std::string& sourcePath,
                        const std::string& outputPath,
                        AssetRecord& record) override;
    AssetType assetType() const override { return AssetType::Audio; }

    // True when the extension is one of the containers this importer accepts.
    static bool isAudioExtension(const std::string& lowercaseExt);
};
