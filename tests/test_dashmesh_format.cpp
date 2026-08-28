// test_dashmesh_format.cpp — .dashmesh v2 (skinning) round-trip + v1 compatibility
//
// Covers: writeDashMesh/readDashMesh, the v1 layout produced before skinning
// existed, and bone weight normalization.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "rendering/animation/DashMeshFile.h"

namespace fs = std::filesystem;
using dash::anim::DashMeshData;
using dash::vkexp::SkinnedVertex;
using dash::vkexp::Vertex;

static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn) do { \
    ++tests_run; \
    std::printf("  [%d] %s ... ", tests_run, #fn); \
    fn(); \
    ++tests_passed; \
    std::printf("PASS\n"); \
} while(0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        std::fprintf(stderr, "FAIL at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        std::abort(); \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::fprintf(stderr, "FAIL at %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
        std::abort(); \
    } \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
    const double _d = static_cast<double>(a) - static_cast<double>(b); \
    if (_d > (eps) || _d < -(eps)) { \
        std::fprintf(stderr, "FAIL at %s:%d: %s (%f) != %s (%f)\n", \
                     __FILE__, __LINE__, #a, static_cast<double>(a), \
                     #b, static_cast<double>(b)); \
        std::abort(); \
    } \
} while(0)

static fs::path tempDir()
{
    static const fs::path dir = fs::temp_directory_path() / "dash_test_dashmesh";
    fs::create_directories(dir);
    return dir;
}

static DashMeshData makeSkinnedMesh()
{
    DashMeshData data;
    data.vertices = {
        Vertex{{{0.0f, 0.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}}, {{0.0f, 0.0f}}},
        Vertex{{{1.0f, 0.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}}, {{1.0f, 0.0f}}},
        Vertex{{{0.5f, 2.0f, 0.0f}}, {{0.0f, 0.0f, 1.0f}}, {{0.5f, 1.0f}}},
    };
    data.skin = {
        SkinnedVertex{{{0, 1, 0, 0}}, {{1.0f, 0.0f, 0.0f, 0.0f}}},
        SkinnedVertex{{{0, 1, 2, 0}}, {{0.5f, 0.3f, 0.2f, 0.0f}}},
        SkinnedVertex{{{2, 3, 1, 0}}, {{0.7f, 0.2f, 0.1f, 0.0f}}},
    };
    data.indices = {0, 1, 2};
    data.diffuseTexturePath = "textures/hero_diffuse.png";
    data.boneCount = 4;
    return data;
}

// ─── Test: v2 round-trip preserves bone indices and weights ──────────────────
static void test_v2_roundtrip_preserves_skinning()
{
    const DashMeshData original = makeSkinnedMesh();
    const std::string path = (tempDir() / "skinned.dashmesh").string();

    std::string error;
    ASSERT_TRUE(dash::anim::writeDashMesh(path, original, error));
    ASSERT_TRUE(error.empty());

    DashMeshData loaded;
    ASSERT_TRUE(dash::anim::readDashMesh(path, loaded, error));

    ASSERT_EQ(loaded.version, dash::anim::kDashMeshVersionSkinned);
    ASSERT_TRUE(loaded.isSkinned());
    ASSERT_EQ(loaded.boneCount, 4u);
    ASSERT_EQ(loaded.vertices.size(), original.vertices.size());
    ASSERT_EQ(loaded.skin.size(), original.skin.size());
    ASSERT_EQ(loaded.indices.size(), original.indices.size());
    ASSERT_EQ(loaded.diffuseTexturePath, original.diffuseTexturePath);

    for (size_t v = 0; v < original.vertices.size(); ++v) {
        for (int c = 0; c < 3; ++c) {
            ASSERT_EQ(loaded.vertices[v].position[c], original.vertices[v].position[c]);
            ASSERT_EQ(loaded.vertices[v].normal[c], original.vertices[v].normal[c]);
        }
        for (int i = 0; i < 4; ++i) {
            ASSERT_EQ(loaded.skin[v].boneIndices[i], original.skin[v].boneIndices[i]);
            ASSERT_EQ(loaded.skin[v].boneWeights[i], original.skin[v].boneWeights[i]);
        }
    }

    for (size_t i = 0; i < original.indices.size(); ++i) {
        ASSERT_EQ(loaded.indices[i], original.indices[i]);
    }

    fs::remove(path);
}

// ─── Test: a mesh with no bones is still written as v1 ───────────────────────
static void test_static_mesh_stays_v1()
{
    DashMeshData data;
    data.vertices = {Vertex{{{1.0f, 2.0f, 3.0f}}, {{0.0f, 1.0f, 0.0f}}, {{0.25f, 0.75f}}}};
    data.indices = {0, 0, 0};

    const std::string path = (tempDir() / "static.dashmesh").string();
    std::string error;
    ASSERT_TRUE(dash::anim::writeDashMesh(path, data, error));

    std::ifstream in(path, std::ios::binary);
    char magic[4] = {};
    uint32_t version = 0;
    in.read(magic, 4);
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.close();

    ASSERT_TRUE(std::memcmp(magic, "DMSH", 4) == 0);
    ASSERT_EQ(version, dash::anim::kDashMeshVersionStatic);

    fs::remove(path);
}

// ─── Test: a hand-written v1 file loads with no bones and no error ───────────
static void test_v1_backward_compatible()
{
    const std::string path = (tempDir() / "legacy_v1.dashmesh").string();

    const uint32_t version = 1;
    const uint32_t vertexCount = 2;
    const uint32_t indexCount = 3;
    const std::string texPath = "old/tex.png";
    const uint16_t texPathLen = static_cast<uint16_t>(texPath.size());

    const std::array<Vertex, 2> vertices = {{
        Vertex{{{1.0f, 2.0f, 3.0f}}, {{0.0f, 1.0f, 0.0f}}, {{0.1f, 0.2f}}},
        Vertex{{{4.0f, 5.0f, 6.0f}}, {{1.0f, 0.0f, 0.0f}}, {{0.3f, 0.4f}}},
    }};
    const std::array<uint32_t, 3> indices = {{0, 1, 0}};

    {
        // Written byte for byte as the pre-skinning importer did.
        std::ofstream out(path, std::ios::binary);
        out.write("DMSH", 4);
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        out.write(reinterpret_cast<const char*>(&vertexCount), sizeof(vertexCount));
        out.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));
        out.write(reinterpret_cast<const char*>(&texPathLen), sizeof(texPathLen));
        out.write(reinterpret_cast<const char*>(vertices.data()), sizeof(Vertex) * vertexCount);
        out.write(reinterpret_cast<const char*>(indices.data()), sizeof(uint32_t) * indexCount);
        out.write(texPath.data(), texPathLen);
    }

    DashMeshData loaded;
    std::string error;
    ASSERT_TRUE(dash::anim::readDashMesh(path, loaded, error));
    ASSERT_TRUE(error.empty());

    ASSERT_EQ(loaded.version, 1u);
    ASSERT_TRUE(loaded.skin.empty());
    ASSERT_TRUE(!loaded.isSkinned());
    ASSERT_EQ(loaded.boneCount, 0u);
    ASSERT_EQ(loaded.vertices.size(), 2u);
    ASSERT_EQ(loaded.indices.size(), 3u);
    ASSERT_EQ(loaded.diffuseTexturePath, texPath);
    ASSERT_EQ(loaded.vertices[1].position[2], 6.0f);
    ASSERT_EQ(loaded.indices[1], 1u);

    fs::remove(path);
}

// ─── Test: garbage input is rejected instead of crashing ─────────────────────
static void test_rejects_bad_magic()
{
    const std::string path = (tempDir() / "garbage.dashmesh").string();
    {
        std::ofstream out(path, std::ios::binary);
        out << "NOPE----------------------------";
    }

    DashMeshData loaded;
    std::string error;
    ASSERT_TRUE(!dash::anim::readDashMesh(path, loaded, error));
    ASSERT_TRUE(!error.empty());

    fs::remove(path);
}

// ─── Test: normalized weights sum to 1 ───────────────────────────────────────
static void test_weights_sum_to_one_after_normalization()
{
    SkinnedVertex v{{{3, 7, 1, 0}}, {{2.0f, 1.0f, 0.5f, 0.5f}}};
    dash::anim::normalizeBoneWeights(v);

    float sum = 0.0f;
    for (float w : v.boneWeights) sum += w;
    ASSERT_NEAR(sum, 1.0f, 1e-5f);

    // Relative proportions survive: the first influence was half of the total.
    ASSERT_NEAR(v.boneWeights[0], 0.5f, 1e-5f);
    ASSERT_EQ(v.boneIndices[1], 7u);
}

// ─── Test: an unweighted vertex is pinned to bone 0 ──────────────────────────
static void test_unweighted_vertex_falls_back_to_bone_zero()
{
    SkinnedVertex v{};
    dash::anim::normalizeBoneWeights(v);

    float sum = 0.0f;
    for (float w : v.boneWeights) sum += w;
    ASSERT_NEAR(sum, 1.0f, 1e-5f);
    ASSERT_EQ(v.boneIndices[0], 0u);
    ASSERT_EQ(v.boneWeights[0], 1.0f);
}

// ─── Test: struct layouts the binary format depends on ───────────────────────
static void test_binary_layout_is_stable()
{
    ASSERT_EQ(sizeof(Vertex), 32u);
    ASSERT_EQ(sizeof(SkinnedVertex), 24u);
    ASSERT_EQ(offsetof(SkinnedVertex, boneIndices), 0u);
    ASSERT_EQ(offsetof(SkinnedVertex, boneWeights), 8u);
}

int main()
{
    std::printf("Running .dashmesh format tests\n");

    RUN_TEST(test_binary_layout_is_stable);
    RUN_TEST(test_v2_roundtrip_preserves_skinning);
    RUN_TEST(test_static_mesh_stays_v1);
    RUN_TEST(test_v1_backward_compatible);
    RUN_TEST(test_rejects_bad_magic);
    RUN_TEST(test_weights_sum_to_one_after_normalization);
    RUN_TEST(test_unweighted_vertex_falls_back_to_bone_zero);

    fs::remove_all(tempDir());

    std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
