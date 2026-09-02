-- Schema v3: item definitions used by loot tables, equipment and consumables.

CREATE TABLE IF NOT EXISTS items (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    description TEXT NOT NULL,
    item_type TEXT NOT NULL,
    rarity TEXT NOT NULL,
    icon TEXT NOT NULL DEFAULT '',
    level_req INTEGER NOT NULL DEFAULT 1,
    gold_value INTEGER NOT NULL DEFAULT 0,
    stackable INTEGER NOT NULL DEFAULT 0,
    max_stack INTEGER NOT NULL DEFAULT 1,
    bonus_attack INTEGER NOT NULL DEFAULT 0,
    bonus_defense INTEGER NOT NULL DEFAULT 0,
    bonus_magic_attack INTEGER NOT NULL DEFAULT 0,
    bonus_speed REAL NOT NULL DEFAULT 0,
    bonus_crit_chance REAL NOT NULL DEFAULT 0,
    bonus_max_hp INTEGER NOT NULL DEFAULT 0,
    bonus_max_mana INTEGER NOT NULL DEFAULT 0,
    consumable_effect TEXT NOT NULL DEFAULT '',
    consumable_value INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_items_type ON items(item_type);
CREATE INDEX IF NOT EXISTS idx_items_rarity ON items(rarity);
