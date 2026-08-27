// ═════════════════════════════════════════════════════════════════════════════
// test_material_asset — Sprint 15: MaterialAsset serialization round-trip
// ═════════════════════════════════════════════════════════════════════════════
#include "assets/MaterialAsset.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

#define ASSERT_FEQ(a, b, msg) ASSERT(std::fabs((a)-(b)) < 0.001f, msg)

static const char* kTempMat = "/tmp/dash_test_material.json";

static void test_roundtrip()
{
    std::printf("  test_roundtrip\n");

    MaterialAsset m;
    m.guid = "mat-abc";
    m.name = "stone";
    m.albedoTexture = "textures/stone.png";
    m.baseColor[0] = 0.5f; m.baseColor[1] = 0.25f; m.baseColor[2] = 0.75f;

    ASSERT(m.saveToFile(kTempMat), "material saved");

    MaterialAsset loaded;
    ASSERT(loaded.loadFromFile(kTempMat), "material loaded");
    ASSERT(loaded.guid == "mat-abc", "guid preserved");
    ASSERT(loaded.name == "stone", "name preserved");
    ASSERT(loaded.albedoTexture == "textures/stone.png", "albedo path preserved");
    ASSERT_FEQ(loaded.baseColor[0], 0.5f,  "baseColor r");
    ASSERT_FEQ(loaded.baseColor[1], 0.25f, "baseColor g");
    ASSERT_FEQ(loaded.baseColor[2], 0.75f, "baseColor b");
}

static void test_defaults_on_empty_json()
{
    std::printf("  test_defaults_on_empty_json\n");

    const MaterialAsset m = MaterialAsset::fromJson(nlohmann::json::object());
    ASSERT(m.name == "default", "defaults to 'default' name");
    ASSERT(m.albedoTexture.empty(), "no albedo by default");
    ASSERT_FEQ(m.baseColor[0], 1.0f, "white base color r");
    ASSERT_FEQ(m.baseColor[2], 1.0f, "white base color b");
}

static void test_missing_file()
{
    std::printf("  test_missing_file\n");

    MaterialAsset m;
    ASSERT(!m.loadFromFile("/tmp/dash_no_such_material_98765.json"), "missing file reports failure");
}

static void test_malformed_json()
{
    std::printf("  test_malformed_json\n");

    const char* path = "/tmp/dash_test_material_bad.json";
    { std::FILE* f = std::fopen(path, "w"); std::fputs("{ not json", f); std::fclose(f); }

    MaterialAsset m;
    ASSERT(!m.loadFromFile(path), "malformed json reports failure");

    std::error_code ec;
    fs::remove(path, ec);
}

int main()
{
    std::printf("=== test_material_asset ===\n");

    test_roundtrip();
    test_defaults_on_empty_json();
    test_missing_file();
    test_malformed_json();

    std::error_code ec;
    fs::remove(kTempMat, ec);

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
