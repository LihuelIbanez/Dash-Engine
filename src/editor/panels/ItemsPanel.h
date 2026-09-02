#pragma once
#include "game/data/GameplayDatabase.h"

#include <functional>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// ItemsPanel — CRUD editor for the item catalog (GameplayDatabase::items()).
// JSON (assets/gameplay/items.json) is the source of truth; SQLite is a cache
// populated by the "Migrate Project Data to SQLite" project action, same as
// player_classes/enemies/loot_tables.
// ─────────────────────────────────────────────────────────────────────────────
class ItemsPanel {
public:
    using LogCallback = std::function<void(const std::string&)>;

    void draw(GameplayDatabase& db, const std::string& assetsRoot, const LogCallback& log);

private:
    void drawList(GameplayDatabase& db);
    void drawInspector(GameplayDatabase& db, const std::string& assetsRoot, const LogCallback& log);

    int  selected_ = -1;
    char filterBuf_[128] = {};
    bool dirty_ = false;
};
