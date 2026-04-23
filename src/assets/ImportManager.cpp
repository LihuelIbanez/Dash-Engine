#include "ImportManager.h"
#include "importers/SceneImporter.h"
#include "importers/TileSetImporter.h"
#include "importers/GameplayConfigImporter.h"
#include "importers/PrefabImporter.h"
#include "importers/SpriteImporter.h"
#include "importers/ModelImporter.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <functional>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Simple hash — using std::hash on file contents (fast, non-crypto)
// ─────────────────────────────────────────────────────────────────────────────
std::string ImportManager::computeFileHash(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};

    std::ostringstream buf;
    buf << in.rdbuf();
    std::string content = buf.str();

    std::size_t h = std::hash<std::string>{}(content);
    std::ostringstream hex;
    hex << std::hex << std::setfill('0') << std::setw(16) << h;
    return hex.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Infer asset type from extension
// ─────────────────────────────────────────────────────────────────────────────
AssetType ImportManager::inferAssetType(const std::string& relativePath)
{
    fs::path p(relativePath);
    std::string ext = p.extension().string();

    // Lowercase
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
        // PNGs inside assets/sprites/ are registered as Sprite assets
        std::string parent = p.parent_path().filename().string();
        if (parent == "sprites") return AssetType::Sprite;
        return AssetType::Texture;
    }

    if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
        return AssetType::Model;
    }

    if (ext == ".json") {
        // Try to differentiate by parent folder or filename convention
        std::string stem = p.stem().string();
        std::string parent = p.parent_path().filename().string();

        if (parent == "scenes" || stem.find("scene") != std::string::npos)
            return AssetType::Scene;
        if (parent == "tilesets" || stem.find("tileset") != std::string::npos)
            return AssetType::TileSet;
        if (parent == "prefabs")
            return AssetType::Prefab;
        if (parent == "config" || stem.find("config") != std::string::npos)
            return AssetType::GameplayConfig;

        // Default JSON → GameplayConfig
        return AssetType::GameplayConfig;
    }

    return AssetType::Unknown;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor — register importers
// ─────────────────────────────────────────────────────────────────────────────
ImportManager::ImportManager()
{
    importers_[AssetType::Scene]          = std::make_unique<SceneImporter>();
    importers_[AssetType::TileSet]        = std::make_unique<TileSetImporter>();
    importers_[AssetType::GameplayConfig] = std::make_unique<GameplayConfigImporter>();
    importers_[AssetType::Prefab]         = std::make_unique<PrefabImporter>();
    importers_[AssetType::Sprite]         = std::make_unique<SpriteImporter>();
    importers_[AssetType::Model]          = std::make_unique<ModelImporter>();
}

// ─────────────────────────────────────────────────────────────────────────────
// importAsset — incremental import for a single file
// ─────────────────────────────────────────────────────────────────────────────
bool ImportManager::importAsset(const std::string& assetsRoot,
                                const std::string& libraryRoot,
                                const std::string& relativePath,
                                AssetDatabase& db,
                                std::vector<std::string>& outErrors,
                                bool force)
{
    std::string fullSource = (fs::path(assetsRoot) / relativePath).string();

    // Compute hash
    std::string newHash = computeFileHash(fullSource);
    if (newHash.empty()) {
        outErrors.push_back("Cannot read: " + relativePath);
        return false;
    }

    // Check if already imported with same hash
    const AssetRecord* existing = db.findBySourcePath(relativePath);
    if (existing && existing->hash == newHash && !force) {
        return false; // up to date
    }

    AssetType type = inferAssetType(relativePath);
    auto it = importers_.find(type);
    if (it == importers_.end()) {
        // No importer for this type — just register without importing
        AssetRecord rec;
        if (existing) rec = *existing;
        rec.sourcePath = relativePath;
        rec.assetType  = type;
        rec.hash       = newHash;
        rec.lastImportTime = std::chrono::system_clock::to_time_t(
                                std::chrono::system_clock::now());
        db.upsertRecord(rec);
        return true;
    }

    // Build output path: library/<relativePath>
    std::string outputPath = (fs::path(libraryRoot) / relativePath).string();

    AssetRecord rec;
    if (existing) rec = *existing;
    rec.sourcePath = relativePath;

    ImportResult ir = it->second->import(fullSource, outputPath, rec);

    rec.hash           = newHash;
    rec.importPath     = relativePath; // relative inside library/
    rec.lastImportTime = std::chrono::system_clock::to_time_t(
                            std::chrono::system_clock::now());
    rec.dependencies   = ir.dependencyGuids;

    db.upsertRecord(rec);

    for (auto& err : ir.errors)
        outErrors.push_back("[" + relativePath + "] " + err);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// reimportChanged — reimport only changed assets reported by FileWatcher
// ─────────────────────────────────────────────────────────────────────────────
bool ImportManager::reimportChanged(const std::vector<FileWatcher::FileChange>& changes,
                                    const std::string& assetsRoot,
                                    const std::string& libraryRoot,
                                    AssetDatabase& db,
                                    std::vector<std::string>& outErrors)
{
    bool anyChanged = false;
    for (const auto& change : changes) {
        if (change.type == FileWatcher::FileChange::Added ||
            change.type == FileWatcher::FileChange::Modified) {
            if (importAsset(assetsRoot, libraryRoot, change.relativePath,
                            db, outErrors, /*force=*/true))
                anyChanged = true;
        } else if (change.type == FileWatcher::FileChange::Deleted) {
            db.removeBySourcePath(change.relativePath);
            anyChanged = true;
        }
    }
    return anyChanged;
}

// ─────────────────────────────────────────────────────────────────────────────
// importAll — scan assets/ and import everything that changed
// ─────────────────────────────────────────────────────────────────────────────
int ImportManager::importAll(const std::string& assetsRoot,
                             const std::string& libraryRoot,
                             AssetDatabase& db,
                             std::vector<std::string>& outErrors)
{
    int imported = 0;
    if (!fs::is_directory(assetsRoot)) return 0;

    for (auto& entry : fs::recursive_directory_iterator(
            assetsRoot, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file()) continue;

        std::string rel = fs::relative(entry.path(), assetsRoot).string();

        // Skip hidden files and asset_db.json itself
        if (rel[0] == '.' || rel == "asset_db.json") continue;

        if (importAsset(assetsRoot, libraryRoot, rel, db, outErrors))
            ++imported;
    }

    // Prune records whose source was deleted
    db.removeMissingAssets(assetsRoot);

    return imported;
}
