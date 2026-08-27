// ═════════════════════════════════════════════════════════════════════════════
// test_material_importer — backlog A3
//   - inferAssetType reconoce .mat.json
//   - importAll registra el material con GUID, tipo y hash
//   - el reimport incremental detecta cambios de contenido
//   - AssetType sobrevive el round-trip por el asset_db.json
// ═════════════════════════════════════════════════════════════════════════════
#include "AssetDatabase.h"
#include "AssetTypes.h"
#include "ImportManager.h"
#include "MaterialAsset.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

static void writeMaterial(const fs::path& p, const char* name, float r)
{
    fs::create_directories(p.parent_path());
    MaterialAsset m;
    m.name = name;
    m.baseColor[0] = r;
    m.saveToFile(p.string());
}

// ── inferAssetType reconoce el sufijo .mat.json y la carpeta materials/ ──────
static void test_infer_asset_type()
{
    std::printf("  test_infer_asset_type\n");

    ASSERT(ImportManager::inferAssetType("materials/stone.mat.json") == AssetType::Material,
           ".mat.json en materials/ -> Material");
    ASSERT(ImportManager::inferAssetType("props/rusty.mat.json") == AssetType::Material,
           ".mat.json fuera de materials/ -> Material");
    ASSERT(ImportManager::inferAssetType("materials/palette.json") == AssetType::Material,
           ".json dentro de materials/ -> Material");
    ASSERT(ImportManager::inferAssetType("gameplay/enemies.json") == AssetType::GameplayConfig,
           "otros .json siguen siendo GameplayConfig");
}

// ── importAll registra el material con GUID, tipo y hash ────────────────────
static void test_import_registers_material(const fs::path& tmp)
{
    std::printf("  test_import_registers_material\n");

    const fs::path assets = tmp / "assets";
    const fs::path library = tmp / "library";
    writeMaterial(assets / "materials" / "stone.mat.json", "stone", 0.5f);

    AssetDatabase db;
    ImportManager mgr;
    std::vector<std::string> errors;
    const int imported = mgr.importAll(assets.string(), library.string(), db, errors);

    ASSERT(imported == 1, "un asset importado");
    ASSERT(errors.empty(), "sin errores de import");

    const AssetRecord* rec = db.findBySourcePath("materials/stone.mat.json");
    ASSERT(rec != nullptr, "el material quedo registrado en la base");
    if (!rec) return;

    ASSERT(rec->assetType == AssetType::Material, "tipo Material");
    ASSERT(!rec->guid.empty(), "tiene GUID asignado");
    ASSERT(!rec->hash.empty(), "tiene hash");
    ASSERT(fs::exists(library / "materials" / "stone.mat.json"),
           "el material se copio a library/");

    // findByGuid es el camino que usa el renderer para resolver la referencia.
    ASSERT(db.findByGuid(rec->guid) != nullptr, "resoluble por GUID");
}

// ── Reimport incremental: sin cambios no reimporta, con cambios si ──────────
static void test_incremental_reimport(const fs::path& tmp)
{
    std::printf("  test_incremental_reimport\n");

    const fs::path assets = tmp / "assets";
    const fs::path library = tmp / "library";
    const fs::path mat = assets / "materials" / "stone.mat.json";
    writeMaterial(mat, "stone", 0.5f);

    AssetDatabase db;
    ImportManager mgr;
    std::vector<std::string> errors;

    mgr.importAll(assets.string(), library.string(), db, errors);
    const AssetRecord* first = db.findBySourcePath("materials/stone.mat.json");
    ASSERT(first != nullptr, "primer import ok");
    if (!first) return;
    const std::string guid = first->guid;
    const std::string hash = first->hash;

    ASSERT(mgr.importAll(assets.string(), library.string(), db, errors) == 0,
           "sin cambios no reimporta nada");

    writeMaterial(mat, "stone", 0.9f);
    ASSERT(mgr.importAll(assets.string(), library.string(), db, errors) == 1,
           "el cambio de contenido dispara un reimport");

    const AssetRecord* second = db.findBySourcePath("materials/stone.mat.json");
    ASSERT(second != nullptr, "sigue registrado tras el reimport");
    if (!second) return;
    ASSERT(second->guid == guid, "el GUID se conserva entre reimports");
    ASSERT(second->hash != hash, "el hash cambio");
}

// ── El AssetType sobrevive guardar y recargar el asset_db.json ──────────────
static void test_asset_type_round_trip(const fs::path& tmp)
{
    std::printf("  test_asset_type_round_trip\n");

    const fs::path dbPath = tmp / "asset_db.json";

    AssetDatabase db;
    const AssetType types[] = {AssetType::Material, AssetType::Prefab,
                               AssetType::Sprite, AssetType::Model,
                               AssetType::Texture, AssetType::Scene};
    for (AssetType t : types) {
        AssetRecord rec;
        rec.guid = std::string("guid-") + assetTypeToStr(t);
        rec.sourcePath = std::string(assetTypeToStr(t)) + "/a.json";
        rec.assetType = t;
        rec.hash = "h";
        db.upsertRecord(rec);
    }
    ASSERT(db.save(dbPath.string()), "asset_db.json guardado");

    AssetDatabase reloaded;
    ASSERT(reloaded.load(dbPath.string()), "asset_db.json recargado");
    for (AssetType t : types) {
        const AssetRecord* rec = reloaded.findByGuid(std::string("guid-") + assetTypeToStr(t));
        const std::string msg = std::string(assetTypeToStr(t)) + " sobrevive el round-trip";
        ASSERT(rec != nullptr && rec->assetType == t, msg.c_str());
    }
}

int main()
{
    std::printf("=== test_material_importer ===\n");

    auto runTest = [](const char* name, auto fn) {
        fs::path tmp = fs::temp_directory_path() / ("dash_mat_test_" + std::string(name));
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        fn(tmp);
        fs::remove_all(tmp);
    };

    test_infer_asset_type();
    runTest("register",   test_import_registers_material);
    runTest("increment",  test_incremental_reimport);
    runTest("roundtrip",  test_asset_type_round_trip);

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
