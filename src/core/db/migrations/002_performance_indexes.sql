-- Schema v2: performance-oriented secondary indexes for common read paths.

CREATE INDEX IF NOT EXISTS idx_scenes_updated_at ON scenes(updated_at);
CREATE INDEX IF NOT EXISTS idx_scenes_scene_name ON scenes(scene_name);

CREATE INDEX IF NOT EXISTS idx_savegames_updated_at ON savegames(updated_at);

CREATE INDEX IF NOT EXISTS idx_loot_table_enemies_enemy_id ON loot_table_enemies(enemy_id);
CREATE INDEX IF NOT EXISTS idx_loot_drops_loot_id ON loot_drops(loot_id);
