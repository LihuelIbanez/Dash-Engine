// test_model_import_pipeline.cpp — Sprint 11 QA smoke tests
// Tests: ModelImporter, Vertex layout, AssetType::Model, MeshData, AssetCache3D

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <string>

#include "AssetTypes.h"
#include "rendering/mesh/Vertex.h"
#include "rendering/mesh/MeshData.h"
#include "importers/ModelImporter.h"

namespace fs = std::filesystem;

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

// ─── Test: Vertex layout size ────────────────────────────────────────────────
static void test_vertex_layout_size()
{
    // 3 floats position + 3 floats normal + 2 floats texCoord = 8 floats = 32 bytes
    ASSERT_EQ(sizeof(dash::vkexp::Vertex), 32u);

    // Check offsets
    ASSERT_EQ(offsetof(dash::vkexp::Vertex, position), 0u);
    ASSERT_EQ(offsetof(dash::vkexp::Vertex, normal), 12u);
    ASSERT_EQ(offsetof(dash::vkexp::Vertex, texCoord), 24u);
}

// ─── Test: AssetType::Model exists ───────────────────────────────────────────
static void test_asset_type_model()
{
    ASSERT_EQ(assetTypeToStr(AssetType::Model), std::string("Model"));
    ASSERT_TRUE(AssetType::Model != AssetType::Unknown);
    ASSERT_TRUE(AssetType::Model != AssetType::Texture);
}

// ─── Test: MeshData struct ───────────────────────────────────────────────────
static void test_mesh_data_struct()
{
    dash::vkexp::MeshData data;
    ASSERT_TRUE(data.vertices.empty());
    ASSERT_TRUE(data.indices.empty());
    ASSERT_TRUE(data.diffuseTexturePath.empty());

    data.vertices.push_back({{1.0f, 2.0f, 3.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}});
    data.indices.push_back(0);
    data.diffuseTexturePath = "textures/test.png";

    ASSERT_EQ(data.vertices.size(), 1u);
    ASSERT_EQ(data.indices.size(), 1u);
    ASSERT_EQ(data.vertices[0].position[0], 1.0f);
    ASSERT_EQ(data.vertices[0].normal[1], 1.0f);
    ASSERT_EQ(data.vertices[0].texCoord[0], 0.5f);
}

// ─── Test: ModelImporter with valid .obj ─────────────────────────────────────
static void test_assimp_loads_obj()
{
    // Create a minimal .obj file
    auto tmpDir = fs::temp_directory_path() / "dash_test_model";
    fs::create_directories(tmpDir);

    std::string objPath = (tmpDir / "cube.obj").string();
    {
        std::ofstream out(objPath);
        out << "# Minimal cube\n"
            << "v -1 -1 -1\n"
            << "v  1 -1 -1\n"
            << "v  1  1 -1\n"
            << "v -1  1 -1\n"
            << "f 1 2 3\n"
            << "f 1 3 4\n";
    }

    std::string outPath = (tmpDir / "cube_out").string();
    AssetRecord record;
    ModelImporter importer;

    ImportResult result = importer.import(objPath, outPath, record);
    ASSERT_TRUE(result.success);
    ASSERT_TRUE(result.errors.empty());
    ASSERT_EQ(record.assetType, AssetType::Model);

    // Verify .dashmesh output exists
    std::string dashmeshPath = outPath + ".dashmesh";
    // The importer replaces extension, so check with replaced extension
    fs::path outP(outPath);
    outP.replace_extension(".dashmesh");
    ASSERT_TRUE(fs::exists(outP));

    // Verify the binary file has content
    ASSERT_TRUE(fs::file_size(outP) > 16);  // at least header

    // Read and verify header
    std::ifstream in(outP.string(), std::ios::binary);
    char magic[4];
    in.read(magic, 4);
    ASSERT_EQ(magic[0], 'D');
    ASSERT_EQ(magic[1], 'M');
    ASSERT_EQ(magic[2], 'S');
    ASSERT_EQ(magic[3], 'H');

    uint32_t version, vertexCount, indexCount;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&vertexCount), sizeof(vertexCount));
    in.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount));

    ASSERT_EQ(version, 1u);
    ASSERT_TRUE(vertexCount == 4);  // 4 unique vertices
    ASSERT_TRUE(indexCount == 6);   // 2 triangles * 3

    // Cleanup
    fs::remove_all(tmpDir);
}

// ─── Test: ModelImporter with invalid file ───────────────────────────────────
static void test_assimp_invalid_file()
{
    auto tmpDir = fs::temp_directory_path() / "dash_test_model_bad";
    fs::create_directories(tmpDir);

    std::string badPath = (tmpDir / "corrupt.obj").string();
    {
        std::ofstream out(badPath);
        out << "this is not a valid obj file\ngarbage data\n";
    }

    std::string outPath = (tmpDir / "corrupt_out").string();
    AssetRecord record;
    ModelImporter importer;

    ImportResult result = importer.import(badPath, outPath, record);
    // Assimp may or may not consider random text an error — it might produce 0 meshes
    // Either way it should not crash
    if (!result.success) {
        ASSERT_TRUE(!result.errors.empty());
    }

    // Cleanup
    fs::remove_all(tmpDir);
}

// ─── Test: ModelImporter on nonexistent file ─────────────────────────────────
static void test_assimp_nonexistent_file()
{
    std::string fakePath = "/tmp/dash_nonexistent_model.obj";
    std::string outPath = "/tmp/dash_nonexistent_out";
    AssetRecord record;
    ModelImporter importer;

    ImportResult result = importer.import(fakePath, outPath, record);
    ASSERT_TRUE(!result.success);
    ASSERT_TRUE(!result.errors.empty());
}

// ─── main ────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("\n=== test_model_import_pipeline ===\n");

    RUN_TEST(test_vertex_layout_size);
    RUN_TEST(test_asset_type_model);
    RUN_TEST(test_mesh_data_struct);
    RUN_TEST(test_assimp_loads_obj);
    RUN_TEST(test_assimp_invalid_file);
    RUN_TEST(test_assimp_nonexistent_file);

    std::printf("\n  %d/%d tests passed.\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
