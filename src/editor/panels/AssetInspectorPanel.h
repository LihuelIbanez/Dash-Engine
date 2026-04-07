#pragma once
#include "AssetDatabase.h"
#include "ImportManager.h"
#include <string>
#include <functional>

class AssetInspectorPanel {
public:
    using LogCallback = std::function<void(const std::string&)>;

    void draw(const std::string& selectedGuid,
              AssetDatabase& db,
              ImportManager& importMgr,
              const std::string& assetsRoot,
              const std::string& libraryRoot,
              const std::string& dbPath,
              LogCallback logCb);
};
