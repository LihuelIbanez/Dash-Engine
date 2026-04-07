#include "GameplayDatabase.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Public
// ─────────────────────────────────────────────────────────────────────────────

bool GameplayDatabase::load(const std::string& assetsDir)
{
    std::string dir = assetsDir;
    if (!dir.empty() && dir.back() != '/') dir += '/';
    dir += "gameplay/";

    bool ok = true;
    ok &= loadPlayerClasses(dir + "player_classes.json");
    ok &= loadEnemies(dir + "enemies.json");
    ok &= loadLootTables(dir + "loot_tables.json");
    return ok;
}

const PlayerClassData* GameplayDatabase::findPlayerClass(const std::string& id) const
{
    auto it = classIndex_.find(id);
    return it != classIndex_.end() ? &playerClasses_[it->second] : nullptr;
}

const EnemyData* GameplayDatabase::findEnemy(const std::string& id) const
{
    auto it = enemyIndex_.find(id);
    return it != enemyIndex_.end() ? &enemies_[it->second] : nullptr;
}

const LootTableData* GameplayDatabase::findLootTableForEnemy(const std::string& enemyId) const
{
    auto it = lootByEnemy_.find(enemyId);
    return it != lootByEnemy_.end() ? &lootTables_[it->second] : nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private loaders
// ─────────────────────────────────────────────────────────────────────────────

bool GameplayDatabase::loadPlayerClasses(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "[GameplayDB] Cannot open %s\n", path.c_str());
        return false;
    }

    json arr = json::parse(f, nullptr, false);
    if (arr.is_discarded() || !arr.is_array()) {
        std::fprintf(stderr, "[GameplayDB] Invalid JSON in %s\n", path.c_str());
        return false;
    }

    for (auto& j : arr) {
        PlayerClassData d;
        d.id          = j.value("id",          "unknown");
        d.name        = j.value("name",        d.id);
        d.description = j.value("description", "");
        d.maxHp       = j.value("maxHp",       100);
        d.maxMana     = j.value("maxMana",     50);
        d.attackCooldown = j.value("attackCooldown", 1.0f);
        if (j.contains("stats")) {
            auto& s = j["stats"];
            d.attack      = s.value("attack",      10);
            d.defense     = s.value("defense",      5);
            d.magicAttack = s.value("magicAttack",  0);
            d.speed       = s.value("speed",        3.0f);
            d.critChance  = s.value("critChance",   0.05f);
        }
        classIndex_[d.id] = playerClasses_.size();
        playerClasses_.push_back(std::move(d));
    }

    std::printf("[GameplayDB] Loaded %zu player classes\n", playerClasses_.size());
    return true;
}

bool GameplayDatabase::loadEnemies(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "[GameplayDB] Cannot open %s\n", path.c_str());
        return false;
    }

    json arr = json::parse(f, nullptr, false);
    if (arr.is_discarded() || !arr.is_array()) {
        std::fprintf(stderr, "[GameplayDB] Invalid JSON in %s\n", path.c_str());
        return false;
    }

    for (auto& j : arr) {
        EnemyData d;
        d.id              = j.value("id",              "unknown");
        d.name            = j.value("name",            d.id);
        d.maxHp           = j.value("maxHp",           60);
        d.detectionRadius = j.value("detectionRadius",  6.0f);
        d.attackRadius    = j.value("attackRadius",     1.2f);
        d.expReward       = j.value("expReward",        40);
        d.attackCooldown  = j.value("attackCooldown",   1.0f);
        if (j.contains("stats")) {
            auto& s = j["stats"];
            d.attack      = s.value("attack",      10);
            d.defense     = s.value("defense",      5);
            d.magicAttack = s.value("magicAttack",  0);
            d.speed       = s.value("speed",        2.5f);
            d.critChance  = s.value("critChance",   0.05f);
        }
        enemyIndex_[d.id] = enemies_.size();
        enemies_.push_back(std::move(d));
    }

    std::printf("[GameplayDB] Loaded %zu enemy types\n", enemies_.size());
    return true;
}

bool GameplayDatabase::loadLootTables(const std::string& path)
{
    std::ifstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "[GameplayDB] Cannot open %s\n", path.c_str());
        return false;
    }

    json arr = json::parse(f, nullptr, false);
    if (arr.is_discarded() || !arr.is_array()) {
        std::fprintf(stderr, "[GameplayDB] Invalid JSON in %s\n", path.c_str());
        return false;
    }

    for (auto& j : arr) {
        LootTableData d;
        d.id = j.value("id", "unknown");
        if (j.contains("enemies")) {
            for (auto& eid : j["enemies"])
                d.enemies.push_back(eid.get<std::string>());
        }
        if (j.contains("drops")) {
            for (auto& dj : j["drops"]) {
                LootDrop ld;
                ld.item   = dj.value("item",   "unknown");
                ld.chance = dj.value("chance",  0.0f);
                ld.minQty = dj.value("minQty",  1);
                ld.maxQty = dj.value("maxQty",  1);
                d.drops.push_back(std::move(ld));
            }
        }
        std::size_t idx = lootTables_.size();
        for (auto& eid : d.enemies)
            lootByEnemy_[eid] = idx;
        lootTables_.push_back(std::move(d));
    }

    std::printf("[GameplayDB] Loaded %zu loot tables\n", lootTables_.size());
    return true;
}
