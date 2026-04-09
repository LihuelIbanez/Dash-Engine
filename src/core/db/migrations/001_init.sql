-- Schema v1: foundational tables for project metadata and content storage.

CREATE TABLE IF NOT EXISTS project_meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS assets (
    guid TEXT PRIMARY KEY,
    source_path TEXT NOT NULL,
    import_path TEXT NOT NULL,
    asset_type TEXT NOT NULL,
    hash TEXT NOT NULL,
    last_import_time INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS asset_dependencies (
    asset_guid TEXT NOT NULL,
    dependency_path TEXT NOT NULL,
    PRIMARY KEY(asset_guid, dependency_path),
    FOREIGN KEY(asset_guid) REFERENCES assets(guid) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_assets_source_path ON assets(source_path);
CREATE INDEX IF NOT EXISTS idx_assets_type ON assets(asset_type);

CREATE TABLE IF NOT EXISTS player_classes (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    description TEXT NOT NULL,
    max_hp INTEGER NOT NULL,
    max_mana INTEGER NOT NULL,
    attack_cooldown REAL NOT NULL,
    attack INTEGER NOT NULL,
    defense INTEGER NOT NULL,
    magic_attack INTEGER NOT NULL,
    speed REAL NOT NULL,
    crit_chance REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS enemies (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    max_hp INTEGER NOT NULL,
    detection_radius REAL NOT NULL,
    attack_radius REAL NOT NULL,
    exp_reward INTEGER NOT NULL,
    attack_cooldown REAL NOT NULL,
    attack INTEGER NOT NULL,
    defense INTEGER NOT NULL,
    magic_attack INTEGER NOT NULL,
    speed REAL NOT NULL,
    crit_chance REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS loot_tables (
    id TEXT PRIMARY KEY
);

CREATE TABLE IF NOT EXISTS loot_table_enemies (
    loot_id TEXT NOT NULL,
    enemy_id TEXT NOT NULL,
    PRIMARY KEY(loot_id, enemy_id),
    FOREIGN KEY(loot_id) REFERENCES loot_tables(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS loot_drops (
    loot_id TEXT NOT NULL,
    item TEXT NOT NULL,
    chance REAL NOT NULL,
    min_qty INTEGER NOT NULL,
    max_qty INTEGER NOT NULL,
    PRIMARY KEY(loot_id, item),
    FOREIGN KEY(loot_id) REFERENCES loot_tables(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS scenes (
    scene_id TEXT PRIMARY KEY,
    file_name TEXT NOT NULL UNIQUE,
    scene_name TEXT NOT NULL,
    world_seed INTEGER NOT NULL,
    next_entity_id INTEGER NOT NULL,
    scene_version INTEGER NOT NULL,
    raw_json TEXT NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS savegames (
    save_id TEXT PRIMARY KEY,
    slot_name TEXT NOT NULL UNIQUE,
    save_version INTEGER NOT NULL,
    raw_json TEXT NOT NULL,
    updated_at INTEGER NOT NULL
);
