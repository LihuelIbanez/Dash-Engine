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
// Item catalog — used by loot tables, equipment and consumables. Rarity/type
// are strings on disk (JSON + SQLite) for readability; the enums below are the
// in-memory representation the editor and gameplay code operate on.
// ─────────────────────────────────────────────────────────────────────────────
enum class ItemType : int { Weapon = 0, Armor = 1, Consumable = 2, Material = 3, Quest = 4, Misc = 5 };
enum class ItemRarity : int { Normal = 0, Magic = 1, Rare = 2, Legendary = 3, Unique = 4 };

inline const char* itemTypeToStr(ItemType t) {
    switch (t) {
        case ItemType::Weapon:     return "weapon";
        case ItemType::Armor:      return "armor";
        case ItemType::Consumable: return "consumable";
        case ItemType::Material:   return "material";
        case ItemType::Quest:      return "quest";
        default:                   return "misc";
    }
}

inline ItemType itemTypeFromStr(const std::string& s) {
    if (s == "weapon")     return ItemType::Weapon;
    if (s == "armor")      return ItemType::Armor;
    if (s == "consumable") return ItemType::Consumable;
    if (s == "material")   return ItemType::Material;
    if (s == "quest")      return ItemType::Quest;
    return ItemType::Misc;
}

inline const char* itemRarityToStr(ItemRarity r) {
    switch (r) {
        case ItemRarity::Normal:    return "normal";
        case ItemRarity::Magic:     return "magic";
        case ItemRarity::Rare:      return "rare";
        case ItemRarity::Legendary: return "legendary";
        default:                    return "unique";
    }
}

inline ItemRarity itemRarityFromStr(const std::string& s) {
    if (s == "normal")    return ItemRarity::Normal;
    if (s == "magic")     return ItemRarity::Magic;
    if (s == "rare")      return ItemRarity::Rare;
    if (s == "legendary") return ItemRarity::Legendary;
    return ItemRarity::Unique;
}

struct ItemData {
    std::string id;
    std::string name;
    std::string description;
    ItemType    type   = ItemType::Misc;
    ItemRarity  rarity = ItemRarity::Normal;
    std::string icon;
    int         levelReq  = 1;
    int         goldValue = 0;
    bool        stackable = false;
    int         maxStack  = 1;
    // Equipment bonuses (0 = no effect on that stat)
    int         bonusAttack      = 0;
    int         bonusDefense     = 0;
    int         bonusMagicAttack = 0;
    float       bonusSpeed       = 0.f;
    float       bonusCritChance  = 0.f;
    int         bonusMaxHp       = 0;
    int         bonusMaxMana     = 0;
    // Consumables (e.g. effect="heal", value=50)
    std::string consumableEffect;
    int         consumableValue = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// GameplayDatabase – loads and provides access to gameplay data
// ─────────────────────────────────────────────────────────────────────────────
class GameplayDatabase {
public:
    // Load all JSON files from `assetsDir/gameplay/`
    bool load(const std::string& assetsDir);

    // Lookups (return nullptr if not found)
    const PlayerClassData* findPlayerClass(const std::string& id) const;
    const EnemyData*       findEnemy(const std::string& id) const;
    const LootTableData*   findLootTableForEnemy(const std::string& enemyId) const;
    const ItemData*        findItem(const std::string& id) const;

    const std::vector<PlayerClassData>& playerClasses() const { return playerClasses_; }
    const std::vector<EnemyData>&       enemies()       const { return enemies_; }
    const std::vector<LootTableData>&   lootTables()    const { return lootTables_; }
    const std::vector<ItemData>&        items()         const { return items_; }

    // Editor CRUD: mutate in place, then call rebuildXIndex() and saveXToJson()
    // to persist. JSON stays the source of truth, matching the rest of the
    // gameplay tables (SQLite is a cache populated by the "Migrate Project
    // Data to SQLite" project action).
    std::vector<ItemData>& itemsMutable() { return items_; }
    void rebuildItemIndex();
    bool saveItemsToJson(const std::string& path) const;

    std::vector<EnemyData>& enemiesMutable() { return enemies_; }
    void rebuildEnemyIndex();
    bool saveEnemiesToJson(const std::string& path) const;

    std::vector<PlayerClassData>& playerClassesMutable() { return playerClasses_; }
    void rebuildClassIndex();
    bool savePlayerClassesToJson(const std::string& path) const;

private:
    std::vector<PlayerClassData> playerClasses_;
    std::vector<EnemyData>       enemies_;
    std::vector<LootTableData>   lootTables_;
    std::vector<ItemData>        items_;

    std::unordered_map<std::string, std::size_t> classIndex_;
    std::unordered_map<std::string, std::size_t> enemyIndex_;
    std::unordered_map<std::string, std::size_t> lootByEnemy_; // enemyId → lootTable index
    std::unordered_map<std::string, std::size_t> itemIndex_;

    bool loadPlayerClasses(const std::string& path);
    bool loadEnemies(const std::string& path);
    bool loadLootTables(const std::string& path);
    bool loadItems(const std::string& path);

    bool loadFromSqlite(const std::string& dbPath);
    bool loadPlayerClassesFromSqlite();
    bool loadEnemiesFromSqlite();
    bool loadLootTablesFromSqlite();
    bool loadItemsFromSqlite();

    void clearAll();

    std::string sqlitePath_;
};
