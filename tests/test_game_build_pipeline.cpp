#include "project/GameBuildPipeline.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

static void writeText(const fs::path& p, const std::string& data)
{
    fs::create_directories(p.parent_path());
    std::ofstream o(p);
    o << data;
}

int main()
{
    std::printf("=== test_game_build_pipeline ===\n");

    fs::path tmpRoot = fs::temp_directory_path() / "dash_test_build_pipeline";
    fs::path projectRoot = tmpRoot / "project";
    fs::path buildDir = tmpRoot / "build";
    fs::path outputDir = tmpRoot / "out";
    std::error_code ec;
    fs::remove_all(tmpRoot, ec);

    fs::create_directories(projectRoot / "assets" / "sprites", ec);
    fs::create_directories(projectRoot / "scenes", ec);
    fs::create_directories(buildDir / "src" / "game", ec);

    writeText(projectRoot / "assets" / "sprites" / "hero.png", "fakepng");
    writeText(projectRoot / "scenes" / "default.json", "{}");

    // Fake build output executable so pipeline can run in test mode.
    fs::path fakeExe = buildDir / "VulkanBootstrap";
    writeText(fakeExe, "#!/bin/sh\necho fake\n");
    fs::permissions(fakeExe,
                    fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace,
                    ec);

    ProjectManifest m;
    m.name = "BundleTest";
    m.projectRoot = projectRoot.string();

    setenv("DASH_SKIP_GAME_BUILD", "1", 1);
    auto res = GameBuildPipeline::build(m, outputDir.string(), buildDir.string());
    unsetenv("DASH_SKIP_GAME_BUILD");

    ASSERT(res.success, "pipeline success");
    ASSERT(!res.outputPath.empty(), "output path returned");

#if defined(__APPLE__)
    fs::path app = fs::path(res.outputPath);
    ASSERT(fs::exists(app), "app bundle exists");
    ASSERT(fs::exists(app / "Contents" / "MacOS" / "BundleTest"), "app executable copied");
    ASSERT(fs::exists(app / "Contents" / "Resources" / "assets" / "sprites" / "hero.png"), "assets copied");
    ASSERT(fs::exists(app / "Contents" / "Resources" / "scenes" / "default.json"), "scenes copied");
    ASSERT(fs::exists(app / "Contents" / "Resources" / "project.json"), "project json written");
#else
    fs::path bundle = fs::path(res.outputPath);
    ASSERT(fs::exists(bundle / "bin" / "VulkanBootstrap"), "bin executable copied");
    ASSERT(fs::exists(bundle / "assets" / "sprites" / "hero.png"), "assets copied");
    ASSERT(fs::exists(bundle / "scenes" / "default.json"), "scenes copied");
    ASSERT(fs::exists(bundle / "project.json"), "project json written");
#endif

    fs::remove_all(tmpRoot, ec);

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
