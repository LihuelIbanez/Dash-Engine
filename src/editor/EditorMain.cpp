#include "EditorApp.h"
#include <cstdio>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char* argv[])
{
    // Force Hybrid mode to enable SQLite with JSON fallback
    setenv("DASH_DB_MODE", "sqlite", 1);
    std::fprintf(stdout, "[EditorMain] Set DASH_DB_MODE=sqlite\n");
    
    EditorApp editor;
    std::string startupProjectPath;
    
    // If project path provided as argument, use it
    if (argc > 1 && argv[1]) {
        startupProjectPath = argv[1];
        std::fprintf(stdout, "[EditorMain] Arg[1] provided: %s\n", startupProjectPath.c_str());
    } else {
        // Auto-discover .dashproject in current working directory
        fs::path cwd = fs::current_path();
        fs::path manifestPath = cwd / ".dashproject";
        std::fprintf(stdout, "[EditorMain] No argv[1], checking cwd: %s\n", cwd.c_str());
        std::fprintf(stdout, "[EditorMain] Manifest path: %s\n", manifestPath.c_str());
        if (fs::exists(manifestPath)) {
            startupProjectPath = manifestPath.string();
            std::fprintf(stdout, "[EditorMain] Auto-discovered project: %s\n", startupProjectPath.c_str());
        } else {
            std::fprintf(stdout, "[EditorMain] No .dashproject found in cwd\n");
        }
    }

    std::fprintf(stdout, "[EditorMain] Calling editor.init with: '%s'\n", startupProjectPath.c_str());
    if (!editor.init(startupProjectPath)) {
        std::fprintf(stderr, "Failed to initialise the editor.\n");
        return 1;
    }
    editor.run();
    return 0;
}
