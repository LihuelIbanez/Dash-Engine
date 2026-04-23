#pragma once
#include "ICommand.h"
#include "SceneData.h"
#include "World.h"
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// FloodFillCommand – flood-fill a region of tiles, storing old state for undo
// ─────────────────────────────────────────────────────────────────────────────
class FloodFillCommand : public ICommand {
public:
    FloodFillCommand(int startX, int startY, TileType newType);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return "Flood Fill"; }

private:
    int      startX_, startY_;
    TileType newType_;

    struct TileRecord {
        int      x, y;
        TileType oldType;
        bool     oldWalkable;
    };
    std::vector<TileRecord> affected_;
    bool captured_ = false;

    static bool isUnwalkable(TileType t);
    void writeTile(SceneData& scene, World& world,
                   int tx, int ty, TileType type, bool walkable);
};
