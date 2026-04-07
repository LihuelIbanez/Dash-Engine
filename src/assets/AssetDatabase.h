#pragma once
#include "AssetRecord.h"
#include <string>
#include <unordered_map>
#include <vector>

class AssetDatabase {
public:
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    void upsertRecord(const AssetRecord& record);
    const AssetRecord* findByGuid(const std::string& guid) const;
    const AssetRecord* findBySourcePath(const std::string& sourcePath) const;
    void removeMissingAssets(const std::string& assetsRoot);

    const std::unordered_map<std::string, AssetRecord>& records() const { return records_; }

private:
    std::unordered_map<std::string, AssetRecord> records_;  // guid -> record
    std::string dbPath_;

    static std::string generateGuid();
    static std::string assetTypeToString(AssetType type);
    static AssetType   stringToAssetType(const std::string& str);
};
