#include "GenerateSettlementsCommand.h"

#include <algorithm>

GenerateSettlementsCommand::GenerateSettlementsCommand(std::vector<Building> buildings,
                                                       std::vector<TileOverride> roadTiles)
    : buildings_(std::move(buildings)), roadTiles_(std::move(roadTiles))
{
}

void GenerateSettlementsCommand::apply(SceneData& scene, World& /*world*/)
{
    for (const Building& b : buildings_) {
        EntityData e;
        e.id         = b.entityId;
        e.type       = EntityData::Type::Enemy;   // inert prop; see PhysicsComponent.isStatic
        e.name       = b.name;
        e.x          = b.x;
        e.y          = b.y;
        e.prefabGuid = b.prefabGuid;
        e.components = b.components;
        scene.entities.push_back(std::move(e));
    }
    for (const TileOverride& t : roadTiles_) scene.tileOverrides.push_back(t);
    scene.modified = true;
}

void GenerateSettlementsCommand::undo(SceneData& scene, World& /*world*/)
{
    for (const Building& b : buildings_) {
        const uint64_t id = b.entityId;
        scene.entities.erase(
            std::remove_if(scene.entities.begin(), scene.entities.end(),
                [id](const EntityData& e) { return e.id == id; }),
            scene.entities.end());
    }

    // Remove exactly the overrides this command added (matched by value, from
    // the back, so undoing right after generating is safe even if the road
    // revisited the same tile more than once).
    for (const TileOverride& t : roadTiles_) {
        auto it = std::find_if(scene.tileOverrides.rbegin(), scene.tileOverrides.rend(),
            [&](const TileOverride& o) {
                return o.x == t.x && o.y == t.y && o.tileType == t.tileType && o.walkable == t.walkable;
            });
        if (it != scene.tileOverrides.rend())
            scene.tileOverrides.erase(std::next(it).base());
    }
    scene.modified = true;
}
