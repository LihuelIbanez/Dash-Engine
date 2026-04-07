#pragma once
#include "AssetDatabase.h"
#include "ImportManager.h"
#include <string>
#include <vector>
#include <functional>

class AssetBrowserPanel {
public:
    using LogCallback = std::function<void(const std::string&)>;

    void draw(AssetDatabase& db,
              ImportManager& importMgr,
              const std::string& assetsRoot,
              const std::string& libraryRoot,
              const std::string& dbPath,
              LogCallback logCb);

    // Currently selected asset GUID (empty = none)
    const std::string& selectedGuid() const { return selectedGuid_; }

private:
    std::string selectedGuid_;
    char filterBuf_[128] = {};
};
