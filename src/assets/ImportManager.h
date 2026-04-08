#pragma once
#include "AssetDatabase.h"
#include "FileWatcher.h"
#include "importers/IImporter.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

class ImportManager {
public:
    ImportManager();

    // Import a single asset. Returns true if import ran (hash changed or forced).
    // Errors are collected in outErrors.
    bool importAsset(const std::string& assetsRoot,
                     const std::string& libraryRoot,
                     const std::string& relativePath,
                     AssetDatabase& db,
                     std::vector<std::string>& outErrors,
                     bool force = false);

    // Scan assets/ and import everything that changed.
    int importAll(const std::string& assetsRoot,
                  const std::string& libraryRoot,
                  AssetDatabase& db,
                  std::vector<std::string>& outErrors);

    // Compute SHA-256 hash of file content (hex string).
    static std::string computeFileHash(const std::string& path);

    // Infer asset type from file extension.
    static AssetType inferAssetType(const std::string& relativePath);

    // Reimport only the files listed in changes (from FileWatcher).
    // Returns true if any record was modified.
    bool reimportChanged(const std::vector<FileWatcher::FileChange>& changes,
                         const std::string& assetsRoot,
                         const std::string& libraryRoot,
                         AssetDatabase& db,
                         std::vector<std::string>& outErrors);

private:
    std::unordered_map<AssetType, std::unique_ptr<IImporter>> importers_;
};
