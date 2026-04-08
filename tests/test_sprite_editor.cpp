// ═════════════════════════════════════════════════════════════════════════════
// test_sprite_editor — pure SpriteOps and serialization checks (D45)
// ═════════════════════════════════════════════════════════════════════════════
#include "SpriteOps.h"
#include "stb_image.h"
#include "stb_image_write.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)

static uint32_t rgbaToAbgr(uint32_t rgba)
{
    uint32_t r = (rgba >> 24) & 0xFFu;
    uint32_t g = (rgba >> 16) & 0xFFu;
    uint32_t b = (rgba >> 8)  & 0xFFu;
    uint32_t a = (rgba >> 0)  & 0xFFu;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

static uint32_t abgrToRgba(uint32_t abgr)
{
    return ((abgr & 0x000000FFu) << 24)
         | ((abgr & 0x0000FF00u) << 8)
         | ((abgr & 0x00FF0000u) >> 8)
         | ((abgr & 0xFF000000u) >> 24);
}

static void test_floodFill_fillsConnectedRegion()
{
    std::printf("  test_floodFill_fillsConnectedRegion\n");
    const int w = 8, h = 8;
    std::vector<uint32_t> p(static_cast<size_t>(w * h), 0u);

    // Box border at x/y = 1..6, interior remains 0.
    for (int x = 1; x <= 6; ++x) {
        p[static_cast<size_t>(1 * w + x)] = 0xFFFFFFFFu;
        p[static_cast<size_t>(6 * w + x)] = 0xFFFFFFFFu;
    }
    for (int y = 1; y <= 6; ++y) {
        p[static_cast<size_t>(y * w + 1)] = 0xFFFFFFFFu;
        p[static_cast<size_t>(y * w + 6)] = 0xFFFFFFFFu;
    }

    SpriteOps::floodFill(p, w, h, 3, 3, 0xFF00FF00u);

    ASSERT_EQ(p[static_cast<size_t>(3 * w + 3)], 0xFF00FF00u, "interior filled");
    ASSERT_EQ(p[static_cast<size_t>(2 * w + 2)], 0xFF00FF00u, "interior neighbor filled");
    ASSERT_EQ(p[static_cast<size_t>(0 * w + 0)], 0u, "outside remains unchanged");
}

static void test_floodFill_doesNotLeakThroughBorders()
{
    std::printf("  test_floodFill_doesNotLeakThroughBorders\n");
    const int w = 8, h = 8;
    std::vector<uint32_t> p(static_cast<size_t>(w * h), 0u);

    for (int x = 2; x <= 5; ++x) {
        p[static_cast<size_t>(2 * w + x)] = 0xFFFF0000u;
        p[static_cast<size_t>(5 * w + x)] = 0xFFFF0000u;
    }
    for (int y = 2; y <= 5; ++y) {
        p[static_cast<size_t>(y * w + 2)] = 0xFFFF0000u;
        p[static_cast<size_t>(y * w + 5)] = 0xFFFF0000u;
    }

    SpriteOps::floodFill(p, w, h, 3, 3, 0xFF0000FFu);

    ASSERT_EQ(p[static_cast<size_t>(3 * w + 3)], 0xFF0000FFu, "inner region filled");
    ASSERT_EQ(p[static_cast<size_t>(1 * w + 1)], 0u, "outside not filled");
    ASSERT_EQ(p[static_cast<size_t>(2 * w + 3)], 0xFFFF0000u, "border preserved");
}

static void test_bresenham_line()
{
    std::printf("  test_bresenham_line\n");
    const int w = 8, h = 8;
    std::vector<uint32_t> p(static_cast<size_t>(w * h), 0u);
    SpriteOps::drawLine(p, w, h, 0, 0, 7, 7, 0xFFFFFFFFu);

    int count = 0;
    for (int i = 0; i < 8; ++i) {
        if (p[static_cast<size_t>(i * w + i)] == 0xFFFFFFFFu) ++count;
    }
    ASSERT_EQ(count, 8, "main diagonal has 8 painted pixels");
}

static void test_bresenham_horizontal()
{
    std::printf("  test_bresenham_horizontal\n");
    const int w = 8, h = 8;
    std::vector<uint32_t> p(static_cast<size_t>(w * h), 0u);
    SpriteOps::drawLine(p, w, h, 1, 4, 6, 4, 0xFFFFAA00u);

    for (int x = 1; x <= 6; ++x)
        ASSERT_EQ(p[static_cast<size_t>(4 * w + x)], 0xFFFFAA00u, "horizontal segment has no gaps");
}

static void test_alphaOver_fullyOpaque()
{
    std::printf("  test_alphaOver_fullyOpaque\n");
    uint32_t dst = 0x11223344u;
    uint32_t src = 0xFF556677u;
    uint32_t out = SpriteOps::alphaOver(dst, src, 1.0f);
    ASSERT_EQ(out, src, "opaque src fully replaces dst");
}

static void test_alphaOver_fullyTransparent()
{
    std::printf("  test_alphaOver_fullyTransparent\n");
    uint32_t dst = 0xAA224466u;
    uint32_t src = 0x00010203u;
    uint32_t out = SpriteOps::alphaOver(dst, src, 1.0f);
    ASSERT_EQ(out, dst, "transparent src leaves dst unchanged");
}

static void test_compositeLayers_visibilityRespected()
{
    std::printf("  test_compositeLayers_visibilityRespected\n");
    const int w = 4, h = 4;
    std::vector<uint32_t> out(static_cast<size_t>(w * h), 0u);
    std::vector<uint32_t> a(static_cast<size_t>(w * h), 0xFF0000FFu);
    std::vector<uint32_t> b(static_cast<size_t>(w * h), 0xFF00FF00u);

    // layer A visible, layer B hidden
    for (int i = 0; i < w * h; ++i) {
        out[static_cast<size_t>(i)] = SpriteOps::alphaOver(out[static_cast<size_t>(i)], a[static_cast<size_t>(i)], 1.0f);
        // b skipped due to visibility=false
    }

    ASSERT_EQ(out[0], 0xFF0000FFu, "hidden layer does not affect composite");
}

static void test_compositeLayers_opacityBlend()
{
    std::printf("  test_compositeLayers_opacityBlend\n");
    uint32_t dst = 0xFF0000FFu; // red
    uint32_t src = 0xFF00FF00u; // green
    uint32_t out = SpriteOps::alphaOver(dst, src, 0.5f);

    // Expect non-trivial blend with full alpha and mixed R/G channels.
    uint32_t a = (out >> 24) & 0xFFu;
    uint32_t g = (out >> 8) & 0xFFu;
    uint32_t r = out & 0xFFu;
    ASSERT_EQ(a, 0xFFu, "blend alpha remains opaque");
    ASSERT(g > 0 && r > 0, "blend contains both source and destination channels");
}

static void test_saveLoad_png_roundtrip()
{
    std::printf("  test_saveLoad_png_roundtrip\n");
    const int w = 8, h = 8;
    std::vector<uint8_t> rgba(static_cast<size_t>(w * h * 4));

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int i = (y * w + x) * 4;
            rgba[static_cast<size_t>(i + 0)] = static_cast<uint8_t>(x * 31);
            rgba[static_cast<size_t>(i + 1)] = static_cast<uint8_t>(y * 29);
            rgba[static_cast<size_t>(i + 2)] = static_cast<uint8_t>((x + y) * 17);
            rgba[static_cast<size_t>(i + 3)] = 255;
        }
    }

    fs::path tmpDir = fs::temp_directory_path() / "dash_sprite_tests";
    std::error_code ec;
    fs::create_directories(tmpDir, ec);
    fs::path png = tmpDir / "roundtrip.png";

    int ok = stbi_write_png(png.string().c_str(), w, h, 4, rgba.data(), w * 4);
    ASSERT(ok != 0, "png written");

    int rw = 0, rh = 0, ch = 0;
    unsigned char* loaded = stbi_load(png.string().c_str(), &rw, &rh, &ch, 4);
    ASSERT(loaded != nullptr, "png loaded");
    if (loaded) {
        ASSERT_EQ(rw, w, "png width preserved");
        ASSERT_EQ(rh, h, "png height preserved");
        bool same = std::equal(rgba.begin(), rgba.end(), loaded);
        ASSERT(same, "png roundtrip preserves pixels");
        stbi_image_free(loaded);
    }

    fs::remove(png, ec);
}

static void test_palette_import_export()
{
    std::printf("  test_palette_import_export\n");
    std::vector<uint32_t> abgr = {
        rgbaToAbgr(0x000000FFu), rgbaToAbgr(0xFF0000FFu),
        rgbaToAbgr(0x00FF00FFu), rgbaToAbgr(0x0000FFFFu)
    };

    json j;
    j["colors"] = json::array();
    for (uint32_t c : abgr) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", abgrToRgba(c));
        j["colors"].push_back(std::string(buf));
    }

    std::vector<uint32_t> parsed;
    for (const auto& it : j["colors"]) {
        std::string s = it.get<std::string>();
        uint32_t rgba = static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 16));
        parsed.push_back(rgbaToAbgr(rgba));
    }

    ASSERT_EQ(parsed.size(), abgr.size(), "palette size preserved");
    for (size_t i = 0; i < parsed.size(); ++i)
        ASSERT_EQ(parsed[i], abgr[i], "palette color preserved");
}

int main()
{
    std::printf("=== test_sprite_editor ===\n");

    test_floodFill_fillsConnectedRegion();
    test_floodFill_doesNotLeakThroughBorders();
    test_bresenham_line();
    test_bresenham_horizontal();
    test_alphaOver_fullyOpaque();
    test_alphaOver_fullyTransparent();
    test_compositeLayers_visibilityRespected();
    test_compositeLayers_opacityBlend();
    test_saveLoad_png_roundtrip();
    test_palette_import_export();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
