#include "AssetDatabase.h"
#include "AssetRepositorySqlite.h"
#include "db/DbMode.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

fs::path sqlitePathForAssetDbPath(const std::string& assetDbJsonPath)
{
    fs::path jsonPath(assetDbJsonPath);
    const fs::path assetsDir = jsonPath.parent_path();
    const fs::path projectRoot = assetsDir.parent_path();
    return projectRoot / ".library" / "dash_engine.db";
}

bool loadJsonAssetDb(const std::string& path,
                     std::unordered_map<std::string, AssetRecord>& records)
{
    records.clear();

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
        rec.assetType      = assetTypeFromStr(item.value("assetType", "Unknown"));
        rec.hash           = item.value("hash", "");
        rec.lastImportTime = item.value("lastImportTime", int64_t(0));
        if (item.contains("dependencies") && item["dependencies"].is_array()) {
            for (auto& dep : item["dependencies"])
                rec.dependencies.push_back(dep.get<std::string>());
        }
        if (!rec.guid.empty())
            records[rec.guid] = std::move(rec);
    }
    return true;
}

bool saveJsonAssetDb(const std::string& path,
                     const std::unordered_map<std::string, AssetRecord>& records)
{
    json root;
    json assets = json::array();

    for (auto& [guid, rec] : records) {
        json item;
        item["guid"]           = rec.guid;
        item["sourcePath"]     = rec.sourcePath;
        item["importPath"]     = rec.importPath;
        item["assetType"]      = assetTypeToStr(rec.assetType);
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

} // namespace

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
    return assetTypeToStr(type);
}

AssetType AssetDatabase::stringToAssetType(const std::string& str)
{
    return assetTypeFromStr(str);
}

// ─────────────────────────────────────────────────────────────────────────────
// load / save
// ─────────────────────────────────────────────────────────────────────────────
bool AssetDatabase::load(const std::string& path)
{
    dbPath_ = path;
    records_.clear();

    const DbMode::Mode mode = DbMode::current();

    if (DbMode::usesSqliteRead(mode)) {
        const fs::path sqlitePath = sqlitePathForAssetDbPath(path);
        if (fs::exists(sqlitePath)) {
            std::string sqliteError;
            if (AssetRepositorySqlite::load(sqlitePath.string(), records_, &sqliteError)) {
                return true;
            }

            if (!DbMode::allowsJsonFallback(mode)) {
                return false;
            }
        }

        if (!DbMode::allowsJsonFallback(mode)) {
            return false;
        }
    }

    return loadJsonAssetDb(path, records_);
}

bool AssetDatabase::save(const std::string& path) const
{
    const DbMode::Mode mode = DbMode::current();
    const bool jsonSaved = DbMode::writesJson(mode) ? saveJsonAssetDb(path, records_) : true;

    if (!DbMode::writesSqlite(mode)) {
        return jsonSaved;
    }

    const fs::path sqlitePath = sqlitePathForAssetDbPath(path);
    std::error_code ec;
    fs::create_directories(sqlitePath.parent_path(), ec);
    if (ec) {
        return jsonSaved;
    }

    std::string sqliteError;
    const bool sqliteSaved = AssetRepositorySqlite::save(sqlitePath.string(), records_, &sqliteError);
    if (sqliteSaved) {
        return true;
    }

    return DbMode::allowsJsonFallback(mode) ? jsonSaved : false;
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
