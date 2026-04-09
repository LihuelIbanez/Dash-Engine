#pragma once

#include "SceneData.h"

#include <string>
#include <vector>

class SceneRepositorySqlite {
public:
    explicit SceneRepositorySqlite(std::string dbPath);

    bool listSceneFiles(std::vector<std::string>& outFiles, std::string* error = nullptr) const;
    bool saveScene(const std::string& fileName, const SceneData& scene, std::string* error = nullptr) const;
    bool loadScene(const std::string& fileName,
                   SceneData& outScene,
                   const std::string& assetsRoot,
                   std::string* error = nullptr) const;

private:
    bool ensureSchema(std::string* error = nullptr) const;

    std::string dbPath_;
};
