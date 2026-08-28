#include "CreateEntityCommand.h"
#include "World.h"

#include <algorithm>

CreateEntityCommand::CreateEntityCommand(EntityData entity, const char* label)
    : entity_(std::move(entity))
    , label_(label ? label : "Create Entity")
{}

void CreateEntityCommand::apply(SceneData& scene, World& /*world*/)
{
    scene.entities.push_back(entity_);
    scene.modified = true;
}

void CreateEntityCommand::undo(SceneData& scene, World& /*world*/)
{
    const uint64_t id = entity_.id;
    // Children would keep a dangling parentId, so detach them first.
    for (auto& e : scene.entities)
        if (e.parentId == id) e.parentId = 0;

    scene.entities.erase(
        std::remove_if(scene.entities.begin(), scene.entities.end(),
                       [id](const EntityData& e) { return e.id == id; }),
        scene.entities.end());
    scene.modified = true;
}
