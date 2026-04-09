#include "AssetDatabase.h"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { \
        ++g_pass; \
    } \
} while(0)

int main()
{
    std::printf("=== test_asset_db_sqlite ===\n");

    const fs::path root = fs::temp_directory_path() / "dash_test_asset_db_sqlite";
    const fs::path assetsDir = root / "assets";
    const fs::path jsonPath = assetsDir / "asset_db.json";
    const fs::path dbPath = root / ".library" / "dash_engine.db";

    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(assetsDir, ec);

    AssetDatabase db;
    AssetRecord rec;
    rec.guid = "asset-guid-1";
    rec.sourcePath = "assets/sprites/player.png";
    rec.importPath = ".library/sprites/player.bin";
    rec.assetType = AssetType::Texture;
    rec.hash = "abc123";
    rec.lastImportTime = 111;
    rec.dependencies = {"assets/sprites/default_palette.json"};

    db.upsertRecord(rec);
    ASSERT(db.save(jsonPath.string()), "save asset db writes json/sqlite");
    ASSERT(fs::exists(dbPath), "sqlite db created");

    AssetDatabase loaded;
    ASSERT(loaded.load(jsonPath.string()), "load asset db");

    const AssetRecord* found = loaded.findByGuid("asset-guid-1");
    ASSERT(found != nullptr, "record loaded by guid");
    if (found) {
        ASSERT(found->sourcePath == rec.sourcePath, "sourcePath matches");
        ASSERT(found->assetType == rec.assetType, "assetType matches");
        ASSERT(found->dependencies.size() == 1, "dependencies loaded");
    }

    fs::remove_all(root, ec);
    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
