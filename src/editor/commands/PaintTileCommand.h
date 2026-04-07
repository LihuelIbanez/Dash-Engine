#pragma once
#include "ICommand.h"
#include "SceneData.h"
#include "World.h"

// ─────────────────────────────────────────────────────────────────────────────
// PaintTileCommand – paint a single tile, storing old state for undo
// ─────────────────────────────────────────────────────────────────────────────
class PaintTileCommand : public ICommand {
public:
    PaintTileCommand(int tx, int ty, TileType newType);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return "Paint Tile"; }

private:
    int      tx_, ty_;
    TileType newType_;

    // Captured on first apply
    TileType oldType_     = TileType::Grass;
    bool     oldWalkable_ = true;
    bool     captured_    = false;

    static bool isUnwalkable(TileType t);
    void   writeTile(SceneData& scene, World& world, TileType type, bool walkable);
};
