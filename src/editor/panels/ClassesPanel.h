#pragma once
#include "game/data/GameplayDatabase.h"

#include <functional>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// ClassesPanel — CRUD editor for player classes (GameplayDatabase::playerClasses()).
// Same list + detail layout as ItemsPanel/BestiaryPanel.
// ─────────────────────────────────────────────────────────────────────────────
class ClassesPanel {
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
