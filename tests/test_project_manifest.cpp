#include "project/ProjectManifest.h"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)

int main()
{
    std::printf("=== test_project_manifest ===\n");

    fs::path tmpRoot = fs::temp_directory_path() / "dash_test_project_manifest";
    std::error_code ec;
    fs::remove_all(tmpRoot, ec);
    fs::create_directories(tmpRoot, ec);

    ProjectManifest m;
    m.name = "ManifestTest";
    m.defaultScene = "scenes/start.json";
    m.assetsDir = "assets";
    m.scenesDir = "scenes";
    m.libraryDir = ".library";
    m.buildOutputDir = "build_output";
    m.gameConfig.screenWidth = 1600;
    m.gameConfig.screenHeight = 900;
    m.gameConfig.targetFps = 120;

    const fs::path manifestPath = tmpRoot / "ManifestTest.dashproject";
    ASSERT(m.saveToFile(manifestPath.string()), "save manifest");

    ProjectManifest loaded;
    ASSERT(loaded.loadFromFile(manifestPath.string()), "load manifest");

    ASSERT_EQ(loaded.name, m.name, "name roundtrip");
    ASSERT_EQ(loaded.defaultScene, m.defaultScene, "defaultScene roundtrip");
    ASSERT_EQ(loaded.assetsDir, m.assetsDir, "assetsDir roundtrip");
    ASSERT_EQ(loaded.scenesDir, m.scenesDir, "scenesDir roundtrip");
    ASSERT_EQ(loaded.libraryDir, m.libraryDir, "libraryDir roundtrip");
    ASSERT_EQ(loaded.buildOutputDir, m.buildOutputDir, "buildOutputDir roundtrip");
    ASSERT_EQ(loaded.gameConfig.screenWidth, m.gameConfig.screenWidth, "screenWidth roundtrip");
    ASSERT_EQ(loaded.gameConfig.screenHeight, m.gameConfig.screenHeight, "screenHeight roundtrip");
    ASSERT_EQ(loaded.gameConfig.targetFps, m.gameConfig.targetFps, "targetFps roundtrip");

    const fs::path canonicalRoot = fs::canonical(tmpRoot, ec);
    ASSERT_EQ(loaded.absoluteAssetsDir(), (canonicalRoot / "assets").string(), "absoluteAssetsDir");
    ASSERT_EQ(loaded.absoluteScenesDir(), (canonicalRoot / "scenes").string(), "absoluteScenesDir");
    ASSERT_EQ(loaded.absoluteLibraryDir(), (canonicalRoot / ".library").string(), "absoluteLibraryDir");
    ASSERT_EQ(loaded.absoluteBuildDir(), (canonicalRoot / "build_output").string(), "absoluteBuildDir");
    ASSERT_EQ(loaded.absoluteDefaultScene(), (canonicalRoot / "scenes/start.json").string(), "absoluteDefaultScene");

    fs::remove_all(tmpRoot, ec);

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
