#include "PaintTileCommand.h"
#include <algorithm>

PaintTileCommand::PaintTileCommand(int tx, int ty, TileType newType)
    : tx_(tx), ty_(ty), newType_(newType)
{}

bool PaintTileCommand::isUnwalkable(TileType t)
{
    return t == TileType::Water || t == TileType::DeepWater ||
           t == TileType::Mountain || t == TileType::Snow;
}

void PaintTileCommand::writeTile(SceneData& scene, World& world,
                                  TileType type, bool walkable)
{
    // Update terrain mesh face
    TerrainFace& f = world.terrain().face(tx_, ty_);
    f.type     = type;
    f.walkable = walkable;
    world.terrain().markDirty();

    // Keep legacy grid in sync
    world.grid[ty_][tx_].type     = type;
    world.grid[ty_][tx_].walkable = walkable;

    TileOverride ovr{tx_, ty_, static_cast<int>(type), walkable};
    bool found = false;
    for (auto& to : scene.tileOverrides) {
        if (to.x == tx_ && to.y == ty_) { to = ovr; found = true; break; }
    }
    if (!found) scene.tileOverrides.push_back(ovr);
    scene.modified = true;
}

void PaintTileCommand::apply(SceneData& scene, World& world)
{
    if (tx_ < 0 || tx_ >= WORLD_W || ty_ < 0 || ty_ >= WORLD_H) return;

    if (!captured_) {
        oldType_     = world.terrain().face(tx_, ty_).type;
        oldWalkable_ = world.terrain().face(tx_, ty_).walkable;
        captured_    = true;
    }

    writeTile(scene, world, newType_, !isUnwalkable(newType_));
}

void PaintTileCommand::undo(SceneData& scene, World& world)
{
    if (tx_ < 0 || tx_ >= WORLD_W || ty_ < 0 || ty_ >= WORLD_H) return;
    writeTile(scene, world, oldType_, oldWalkable_);
}
