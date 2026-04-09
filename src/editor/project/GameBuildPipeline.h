#pragma once

#include "ProjectManifest.h"
#include <string>
#include <vector>

class GameBuildPipeline {
public:
    struct BuildResult {
        bool success = false;
        std::string outputPath;
        std::vector<std::string> log;
    };

    // Build IsometricRPG and export a self-contained game bundle.
    // `outputDir` is the parent output directory. The pipeline creates
    // `<outputDir>/<project-name>_bundle`.
    static BuildResult build(const ProjectManifest& manifest,
                             const std::string& outputDir,
                             const std::string& buildDir);
};
