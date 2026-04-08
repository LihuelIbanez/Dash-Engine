#include "PlaceEnemyCommand.h"
#include "World.h"
#include <algorithm>

PlaceEnemyCommand::PlaceEnemyCommand(float x, float y, uint64_t entityId,
                                     const std::string& enemyName)
{
    entity_.id   = entityId;
    entity_.type = EntityData::Type::Enemy;
    entity_.name = enemyName;
    entity_.x    = x;
    entity_.y    = y;
    entity_.components = {
        TransformComponent{x, y},
        HealthComponent{100, 100},
        StatsComponent{},
        AIComponent{},
        RenderComponent{}
    };
}

void PlaceEnemyCommand::apply(SceneData& scene, World& /*world*/)
{
    scene.entities.push_back(entity_);
    scene.modified = true;
}

void PlaceEnemyCommand::undo(SceneData& scene, World& /*world*/)
{
    uint64_t id = entity_.id;
    scene.entities.erase(
        std::remove_if(scene.entities.begin(), scene.entities.end(),
            [id](const EntityData& e) { return e.id == id; }),
        scene.entities.end());
    scene.modified = true;
}
