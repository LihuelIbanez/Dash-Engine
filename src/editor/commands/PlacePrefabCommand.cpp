#include "PlacePrefabCommand.h"
#include <algorithm>

PlacePrefabCommand::PlacePrefabCommand(float x, float y,
                                       uint64_t entityId,
                                       const std::string& entityName,
                                       const std::string& prefabGuid,
                                       std::vector<ComponentVariant> components)
{
    entity_.id         = entityId;
    entity_.type       = EntityData::Type::Enemy;
    entity_.name       = entityName;
    entity_.x          = x;
    entity_.y          = y;
    entity_.prefabGuid = prefabGuid;
    entity_.components = std::move(components);
    // Sync TransformComponent position.
    for (auto& c : entity_.components) {
        if (auto* t = std::get_if<TransformComponent>(&c)) {
            t->x = x;
            t->y = y;
            break;
        }
    }
}

void PlacePrefabCommand::apply(SceneData& scene, World& /*world*/)
{
    scene.entities.push_back(entity_);
    scene.modified = true;
}

void PlacePrefabCommand::undo(SceneData& scene, World& /*world*/)
{
    uint64_t id = entity_.id;
    scene.entities.erase(
        std::remove_if(scene.entities.begin(), scene.entities.end(),
            [id](const EntityData& e) { return e.id == id; }),
        scene.entities.end());
    scene.modified = true;
}
