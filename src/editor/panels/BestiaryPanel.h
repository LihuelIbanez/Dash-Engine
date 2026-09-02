#pragma once
#include "game/data/GameplayDatabase.h"

#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// BestiaryPanel — tune enemy type stats (GameplayDatabase::enemies()) and
// browse the pre-designed entity (prefab) library side by side, so a designer
// can see both "the stat template" and "the ready-to-place entity" for a
// given monster in one place.
// ─────────────────────────────────────────────────────────────────────────────
class BestiaryPanel {
public:
    using LogCallback = std::function<void(const std::string&)>;

    void draw(GameplayDatabase& db, const std::string& assetsRoot, const LogCallback& log);

private:
    struct PrefabSummary {
        std::string path;
        std::string guid;
        std::string name;
        int         componentCount = 0;
        std::string componentSummary;
    };

    void drawEnemyList(GameplayDatabase& db);
    void drawEnemyInspector(GameplayDatabase& db, const std::string& assetsRoot, const LogCallback& log);
    void drawPrefabLibrary(const std::string& assetsRoot);
    void rescanPrefabs(const std::string& assetsRoot);

    int  selected_ = -1;
    char filterBuf_[128] = {};
    bool dirty_ = false;

    std::vector<PrefabSummary> prefabs_;
    std::string                prefabsScannedRoot_;
};
