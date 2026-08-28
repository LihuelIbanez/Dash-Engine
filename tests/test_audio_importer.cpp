// ═════════════════════════════════════════════════════════════════════════════
// test_audio_importer
//   - inferAssetType reconoce .wav / .mp3 / .flac / .ogg
//   - importAll registra el audio con GUID, tipo y hash, y lo copia a library/
//   - el reimport incremental conserva el GUID
//   - un archivo corrupto se rechaza con error y no llega a library/
//   - AssetType::Audio sobrevive el round-trip por el asset_db.json
// ═════════════════════════════════════════════════════════════════════════════
#include "AssetDatabase.h"
#include "AssetTypes.h"
#include "ImportManager.h"

#include <cstdint>
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

static void put32(std::ofstream& out, uint32_t v)
{
    const unsigned char b[4] = {
        static_cast<unsigned char>(v & 0xFF),
        static_cast<unsigned char>((v >> 8) & 0xFF),
        static_cast<unsigned char>((v >> 16) & 0xFF),
        static_cast<unsigned char>((v >> 24) & 0xFF)};
    out.write(reinterpret_cast<const char*>(b), 4);
}

static void put16(std::ofstream& out, uint16_t v)
{
    const unsigned char b[2] = {
        static_cast<unsigned char>(v & 0xFF),
        static_cast<unsigned char>((v >> 8) & 0xFF)};
    out.write(reinterpret_cast<const char*>(b), 2);
}

// Minimal 16-bit mono PCM WAV built by hand: no binary fixtures in the repo.
static void writeWav(const fs::path& p, int sampleCount, int16_t amplitude)
{
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);

    const uint32_t sampleRate = 8000;
    const uint16_t channels   = 1;
    const uint16_t bits       = 16;
    const uint32_t dataBytes  = static_cast<uint32_t>(sampleCount) * channels * (bits / 8);

    out.write("RIFF", 4);
    put32(out, 36 + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    put32(out, 16);                                   // PCM fmt chunk size
    put16(out, 1);                                    // PCM
    put16(out, channels);
    put32(out, sampleRate);
    put32(out, sampleRate * channels * (bits / 8));   // byte rate
    put16(out, static_cast<uint16_t>(channels * (bits / 8)));
    put16(out, bits);
    out.write("data", 4);
    put32(out, dataBytes);
    for (int i = 0; i < sampleCount; ++i)
        put16(out, static_cast<uint16_t>(static_cast<int16_t>(i % 2 ? amplitude : -amplitude)));
}

// ── Deteccion de tipo por extension ─────────────────────────────────────────
static void test_infer_asset_type()
{
    std::printf("  test_infer_asset_type\n");

    ASSERT(ImportManager::inferAssetType("audio/beep.wav") == AssetType::Audio, ".wav -> Audio");
    ASSERT(ImportManager::inferAssetType("audio/theme.mp3") == AssetType::Audio, ".mp3 -> Audio");
    ASSERT(ImportManager::inferAssetType("audio/step.flac") == AssetType::Audio, ".flac -> Audio");
    ASSERT(ImportManager::inferAssetType("audio/ambient.ogg") == AssetType::Audio, ".ogg -> Audio");
    ASSERT(ImportManager::inferAssetType("sfx/HIT.WAV") == AssetType::Audio,
           "la extension es case-insensitive");
    ASSERT(ImportManager::inferAssetType("audio/notes.json") != AssetType::Audio,
           "un .json en audio/ no es Audio");
    ASSERT(ImportManager::inferAssetType("textures/wall.png") == AssetType::Texture,
           "las texturas no se ven afectadas");
}

// ── Alta con GUID, tipo, hash y copia a library/ ────────────────────────────
static void test_import_registers_audio(const fs::path& tmp)
{
    std::printf("  test_import_registers_audio\n");

    const fs::path assets  = tmp / "assets";
    const fs::path library = tmp / "library";
    writeWav(assets / "audio" / "beep.wav", 64, 1000);

    AssetDatabase db;
    ImportManager mgr;
    std::vector<std::string> errors;
    const int imported = mgr.importAll(assets.string(), library.string(), db, errors);

    ASSERT(imported == 1, "un asset importado");
    ASSERT(errors.empty(), "sin errores de import");

    const AssetRecord* rec = db.findBySourcePath("audio/beep.wav");
    ASSERT(rec != nullptr, "el audio quedo registrado en la base");
    if (!rec) return;

    ASSERT(rec->assetType == AssetType::Audio, "tipo Audio");
    ASSERT(!rec->guid.empty(), "tiene GUID asignado");
    ASSERT(!rec->hash.empty(), "tiene hash");
    ASSERT(fs::exists(library / "audio" / "beep.wav"), "el audio se copio a library/");
    ASSERT(db.findByGuid(rec->guid) != nullptr, "resoluble por GUID");
}

// ── Reimport incremental: sin cambios no reimporta, con cambios si ──────────
static void test_incremental_reimport(const fs::path& tmp)
{
    std::printf("  test_incremental_reimport\n");

    const fs::path assets  = tmp / "assets";
    const fs::path library = tmp / "library";
    const fs::path clip    = assets / "audio" / "beep.wav";
    writeWav(clip, 64, 1000);

    AssetDatabase db;
    ImportManager mgr;
    std::vector<std::string> errors;

    mgr.importAll(assets.string(), library.string(), db, errors);
    const AssetRecord* first = db.findBySourcePath("audio/beep.wav");
    ASSERT(first != nullptr, "primer import ok");
    if (!first) return;
    const std::string guid = first->guid;
    const std::string hash = first->hash;

    ASSERT(mgr.importAll(assets.string(), library.string(), db, errors) == 0,
           "sin cambios no reimporta nada");

    writeWav(clip, 128, 3000);
    ASSERT(mgr.importAll(assets.string(), library.string(), db, errors) == 1,
           "el cambio de contenido dispara un reimport");

    const AssetRecord* second = db.findBySourcePath("audio/beep.wav");
    ASSERT(second != nullptr, "sigue registrado tras el reimport");
    if (!second) return;
    ASSERT(second->guid == guid, "el GUID se conserva entre reimports");
    ASSERT(second->hash != hash, "el hash cambio");
    ASSERT(errors.empty(), "sin errores tras el reimport");
}

// ── Un .wav sin cabecera RIFF/WAVE se rechaza ───────────────────────────────
static void test_rejects_corrupt_file(const fs::path& tmp)
{
    std::printf("  test_rejects_corrupt_file\n");

    const fs::path assets  = tmp / "assets";
    const fs::path library = tmp / "library";
    fs::create_directories(assets / "audio");
    {
        std::ofstream out(assets / "audio" / "broken.wav", std::ios::binary);
        out << "this is definitely not a wave file";
    }
    {   // Empty file: caught before the header check.
        std::ofstream out(assets / "audio" / "empty.wav", std::ios::binary);
    }

    AssetDatabase db;
    ImportManager mgr;
    std::vector<std::string> errors;
    mgr.importAll(assets.string(), library.string(), db, errors);

    ASSERT(errors.size() == 2, "los dos archivos invalidos reportan error");
    ASSERT(!fs::exists(library / "audio" / "broken.wav"),
           "el archivo corrupto no llega a library/");
    ASSERT(!fs::exists(library / "audio" / "empty.wav"),
           "el archivo vacio no llega a library/");

    bool mentionsHeader = false;
    for (const auto& e : errors)
        if (e.find("RIFF") != std::string::npos) mentionsHeader = true;
    ASSERT(mentionsHeader, "el error explica que falta la cabecera RIFF/WAVE");

    // Un archivo valido junto a los rotos sigue importando.
    writeWav(assets / "audio" / "ok.wav", 32, 500);
    errors.clear();
    mgr.importAll(assets.string(), library.string(), db, errors);
    ASSERT(fs::exists(library / "audio" / "ok.wav"), "el archivo valido si se importa");
}

// ── AssetType::Audio sobrevive guardar y recargar el asset_db.json ──────────
static void test_audio_type_round_trip(const fs::path& tmp)
{
    std::printf("  test_audio_type_round_trip\n");

    const fs::path dbPath = tmp / "asset_db.json";

    AssetDatabase db;
    AssetRecord rec;
    rec.guid       = "guid-audio";
    rec.sourcePath = "audio/beep.wav";
    rec.assetType  = AssetType::Audio;
    rec.hash       = "h";
    db.upsertRecord(rec);
    ASSERT(db.save(dbPath.string()), "asset_db.json guardado");

    AssetDatabase reloaded;
    ASSERT(reloaded.load(dbPath.string()), "asset_db.json recargado");
    const AssetRecord* back = reloaded.findByGuid("guid-audio");
    ASSERT(back != nullptr && back->assetType == AssetType::Audio,
           "Audio sobrevive el round-trip");

    ASSERT(assetTypeFromStr(assetTypeToStr(AssetType::Audio)) == AssetType::Audio,
           "Audio ida y vuelta a string");
}

int main()
{
    std::printf("=== test_audio_importer ===\n");

    auto runTest = [](const char* name, auto fn) {
        fs::path tmp = fs::temp_directory_path() / ("dash_audio_test_" + std::string(name));
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        fn(tmp);
        fs::remove_all(tmp);
    };

    test_infer_asset_type();
    runTest("register",  test_import_registers_audio);
    runTest("increment", test_incremental_reimport);
    runTest("corrupt",   test_rejects_corrupt_file);
    runTest("roundtrip", test_audio_type_round_trip);

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
