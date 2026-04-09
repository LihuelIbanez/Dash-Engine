#pragma once

#include "AssetRecord.h"

#include <string>
#include <unordered_map>

class AssetRepositorySqlite {
public:
    static bool load(const std::string& dbPath,
                     std::unordered_map<std::string, AssetRecord>& outRecords,
                     std::string* error = nullptr);

    static bool save(const std::string& dbPath,
                     const std::unordered_map<std::string, AssetRecord>& records,
                     std::string* error = nullptr);
};
