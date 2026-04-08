#include "AssetDatabase.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// GUID generation (random UUID v4)
// ─────────────────────────────────────────────────────────────────────────────
std::string AssetDatabase::generateGuid()
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    auto r = [&]() { return dist(gen); };
    uint32_t a = r(), b = r(), c = r(), d = r();

    // Set version 4 and variant bits
    b = (b & 0xFFFF0FFF) | 0x00004000;
    c = (c & 0x3FFFFFFF) | 0x80000000;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << a << '-'
       << std::setw(4) << (b >> 16) << '-'
       << std::setw(4) << (b & 0xFFFF) << '-'
       << std::setw(4) << (c >> 16) << '-'
       << std::setw(4) << (c & 0xFFFF)
       << std::setw(8) << d;
    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// AssetType <-> string helpers
// ─────────────────────────────────────────────────────────────────────────────
std::string AssetDatabase::assetTypeToString(AssetType type)
{
    switch (type) {
        case AssetType::Texture:        return "Texture";
        case AssetType::TileSet:        return "TileSet";
        case AssetType::Scene:          return "Scene";
        case AssetType::GameplayConfig: return "GameplayConfig";
        default:                        return "Unknown";
    }
}

AssetType AssetDatabase::stringToAssetType(const std::string& str)
{
    if (str == "Texture")        return AssetType::Texture;
    if (str == "TileSet")        return AssetType::TileSet;
    if (str == "Scene")          return AssetType::Scene;
    if (str == "GameplayConfig") return AssetType::GameplayConfig;
    return AssetType::Unknown;
}

// ─────────────────────────────────────────────────────────────────────────────
// load / save
// ─────────────────────────────────────────────────────────────────────────────
bool AssetDatabase::load(const std::string& path)
{
    dbPath_ = path;
    records_.clear();

    std::ifstream in(path);
    if (!in.is_open()) return false;

    json root;
    try {
        root = json::parse(in);
    } catch (...) {
        return false;
    }

    if (!root.is_object() || !root.contains("assets")) return false;

    for (auto& item : root["assets"]) {
        AssetRecord rec;
        rec.guid           = item.value("guid", "");
        rec.sourcePath     = item.value("sourcePath", "");
        rec.importPath     = item.value("importPath", "");
        rec.assetType      = stringToAssetType(item.value("assetType", "Unknown"));
        rec.hash           = item.value("hash", "");
        rec.lastImportTime = item.value("lastImportTime", int64_t(0));
        if (item.contains("dependencies") && item["dependencies"].is_array()) {
            for (auto& dep : item["dependencies"])
                rec.dependencies.push_back(dep.get<std::string>());
        }
        if (!rec.guid.empty())
            records_[rec.guid] = std::move(rec);
    }
    return true;
}

bool AssetDatabase::save(const std::string& path) const
{
    json root;
    json assets = json::array();

    for (auto& [guid, rec] : records_) {
        json item;
        item["guid"]           = rec.guid;
        item["sourcePath"]     = rec.sourcePath;
        item["importPath"]     = rec.importPath;
        item["assetType"]      = assetTypeToString(rec.assetType);
        item["hash"]           = rec.hash;
        item["lastImportTime"] = rec.lastImportTime;
        item["dependencies"]   = rec.dependencies;
        assets.push_back(std::move(item));
    }

    root["assets"] = std::move(assets);

    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << root.dump(2) << '\n';
    return out.good();
}

// ─────────────────────────────────────────────────────────────────────────────
// upsertRecord
// ─────────────────────────────────────────────────────────────────────────────
void AssetDatabase::upsertRecord(const AssetRecord& record)
{
    AssetRecord rec = record;
    if (rec.guid.empty())
        rec.guid = generateGuid();
    records_[rec.guid] = std::move(rec);
}

// ─────────────────────────────────────────────────────────────────────────────
// findByGuid / findBySourcePath
// ─────────────────────────────────────────────────────────────────────────────
const AssetRecord* AssetDatabase::findByGuid(const std::string& guid) const
{
    auto it = records_.find(guid);
    return (it != records_.end()) ? &it->second : nullptr;
}

const AssetRecord* AssetDatabase::findBySourcePath(const std::string& sourcePath) const
{
    for (auto& [guid, rec] : records_)
        if (rec.sourcePath == sourcePath) return &rec;
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// removeMissingAssets - prune records whose source file no longer exists
// ─────────────────────────────────────────────────────────────────────────────
void AssetDatabase::removeBySourcePath(const std::string& sourcePath)
{
    for (auto it = records_.begin(); it != records_.end(); ++it) {
        if (it->second.sourcePath == sourcePath) {
            records_.erase(it);
            return;
        }
    }
}

void AssetDatabase::removeMissingAssets(const std::string& assetsRoot)
{
    for (auto it = records_.begin(); it != records_.end(); ) {
        fs::path full = fs::path(assetsRoot) / it->second.sourcePath;
        if (!fs::exists(full))
            it = records_.erase(it);
        else
            ++it;
    }
}
