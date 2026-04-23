#include "FloodFillCommand.h"
#include <queue>

FloodFillCommand::FloodFillCommand(int startX, int startY, TileType newType)
    : startX_(startX), startY_(startY), newType_(newType)
{}

bool FloodFillCommand::isUnwalkable(TileType t)
{
    return t == TileType::Water || t == TileType::DeepWater ||
           t == TileType::Mountain || t == TileType::Snow;
}

void FloodFillCommand::writeTile(SceneData& scene, World& world,
                                  int tx, int ty, TileType type, bool walkable)
{
    // Update terrain mesh face
    TerrainFace& f = world.terrain().face(tx, ty);
    f.type     = type;
    f.walkable = walkable;

    // Keep legacy grid in sync
    world.grid[ty][tx].type     = type;
    world.grid[ty][tx].walkable = walkable;

    TileOverride ovr{tx, ty, static_cast<int>(type), walkable};
    bool found = false;
    for (auto& to : scene.tileOverrides) {
        if (to.x == tx && to.y == ty) { to = ovr; found = true; break; }
    }
    if (!found) scene.tileOverrides.push_back(ovr);
}

void FloodFillCommand::apply(SceneData& scene, World& world)
{
    if (startX_ < 0 || startX_ >= WORLD_W ||
        startY_ < 0 || startY_ >= WORLD_H) return;

    TileType srcType = world.terrain().face(startX_, startY_).type;
    if (srcType == newType_) return;

    if (!captured_) {
        // BFS flood fill — capture affected tiles
        std::vector<std::vector<bool>> visited(WORLD_H, std::vector<bool>(WORLD_W, false));
        std::queue<std::pair<int,int>> q;
        q.push({startX_, startY_});
        visited[startY_][startX_] = true;

        constexpr int MAX_FILL = 2000;
        int count = 0;

        while (!q.empty() && count < MAX_FILL) {
            auto [cx, cy] = q.front();
            q.pop();

            const TerrainFace& f = world.terrain().face(cx, cy);
            affected_.push_back({cx, cy, f.type, f.walkable});
            ++count;

            constexpr int dx[] = {0, 0, -1, 1};
            constexpr int dy[] = {-1, 1, 0, 0};
            for (int d = 0; d < 4; ++d) {
                int nx = cx + dx[d], ny = cy + dy[d];
                if (nx < 0 || nx >= WORLD_W || ny < 0 || ny >= WORLD_H) continue;
                if (visited[ny][nx]) continue;
                if (world.terrain().face(nx, ny).type != srcType) continue;
                visited[ny][nx] = true;
                q.push({nx, ny});
            }
        }
        captured_ = true;
    }

    bool walkable = !isUnwalkable(newType_);
    for (auto& rec : affected_)
        writeTile(scene, world, rec.x, rec.y, newType_, walkable);

    world.terrain().markDirty();
    scene.modified = true;
}

void FloodFillCommand::undo(SceneData& scene, World& world)
{
    for (auto& rec : affected_)
        writeTile(scene, world, rec.x, rec.y, rec.oldType, rec.oldWalkable);

    world.terrain().markDirty();
    scene.modified = true;
}
