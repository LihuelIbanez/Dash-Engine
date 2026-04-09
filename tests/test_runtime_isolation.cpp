#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

#ifndef PROJECT_DIR
#define PROJECT_DIR "."
#endif

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

static bool containsForbidden(const std::string& content)
{
    return content.find("src/editor/") != std::string::npos
        || content.find("../editor/") != std::string::npos
        || content.find("imgui") != std::string::npos
        || content.find("stb_image_write") != std::string::npos;
}

int main()
{
    std::printf("=== test_runtime_isolation ===\n");

    fs::path gameRoot = fs::path(PROJECT_DIR) / "src" / "game";
    ASSERT(fs::exists(gameRoot), "src/game exists");

    int checked = 0;
    for (auto it = fs::recursive_directory_iterator(gameRoot);
         it != fs::recursive_directory_iterator(); ++it) {
        if (!it->is_regular_file()) continue;

        const fs::path p = it->path();
        const std::string ext = p.extension().string();
        if (ext != ".cpp" && ext != ".h") continue;

        std::ifstream in(p);
        if (!in.is_open()) continue;

        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        bool bad = containsForbidden(content);
        ASSERT(!bad, ("forbidden include/reference in " + p.string()).c_str());
        ++checked;
    }

    ASSERT(checked > 0, "checked at least one runtime source file");

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
