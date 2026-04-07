#pragma once
#include <string>
#include <vector>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// Data structs loaded from JSON — no gameplay logic, pure data
// ─────────────────────────────────────────────────────────────────────────────

struct PlayerClassData {
    std::string id;
    std::string name;
    std::string description;
    int   maxHp       = 100;
    int   maxMana     = 50;
    int   attack      = 10;
    int   defense     = 5;
    int   magicAttack = 0;
    float speed       = 3.0f;
    float critChance  = 0.05f;
    float attackCooldown = 1.0f;
};

struct EnemyData {
    std::string id;
    std::string name;
    int   maxHp       = 60;
    int   attack      = 10;
    int   defense     = 5;
    int   magicAttack = 0;
    float speed       = 2.5f;
    float critChance  = 0.05f;
    float detectionRadius = 6.0f;
    float attackRadius    = 1.2f;
    int   expReward       = 40;
    float attackCooldown  = 1.0f;
};

struct LootDrop {
    std::string item;
    float chance  = 0.0f;
    int   minQty  = 1;
    int   maxQty  = 1;
};

struct LootTableData {
    std::string              id;
    std::vector<std::string> enemies;
    std::vector<LootDrop>    drops;
};

// ─────────────────────────────────────────────────────────────────────────────
// GameplayDatabase – loads and provides read-only access to gameplay data
// ─────────────────────────────────────────────────────────────────────────────
class GameplayDatabase {
public:
    // Load all JSON files from `assetsDir/gameplay/`
    bool load(const std::string& assetsDir);

    // Lookups (return nullptr if not found)
    const PlayerClassData* findPlayerClass(const std::string& id) const;
    const EnemyData*       findEnemy(const std::string& id) const;
    const LootTableData*   findLootTableForEnemy(const std::string& enemyId) const;

    const std::vector<PlayerClassData>& playerClasses() const { return playerClasses_; }
    const std::vector<EnemyData>&       enemies()       const { return enemies_; }
    const std::vector<LootTableData>&   lootTables()    const { return lootTables_; }

private:
    std::vector<PlayerClassData> playerClasses_;
    std::vector<EnemyData>       enemies_;
    std::vector<LootTableData>   lootTables_;

    std::unordered_map<std::string, std::size_t> classIndex_;
    std::unordered_map<std::string, std::size_t> enemyIndex_;
    std::unordered_map<std::string, std::size_t> lootByEnemy_; // enemyId → lootTable index

    bool loadPlayerClasses(const std::string& path);
    bool loadEnemies(const std::string& path);
    bool loadLootTables(const std::string& path);
};
