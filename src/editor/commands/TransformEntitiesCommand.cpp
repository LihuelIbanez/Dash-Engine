#include "TransformEntitiesCommand.h"
#include "World.h"

TransformEntitiesCommand::TransformEntitiesCommand(std::vector<Entry> entries,
                                                   const char* label)
    : entries_(std::move(entries))
    , label_(label ? label : "Transform Entities")
{}

void TransformEntitiesCommand::apply(SceneData& scene, World& /*world*/)
{
    for (const auto& entry : entries_) {
        if (EntityData* e = dash::editor::findEntity(scene, entry.entityId))
            dash::editor::setLocalTransform(*e, entry.newLocal);
    }
    if (!entries_.empty()) scene.modified = true;
}

void TransformEntitiesCommand::undo(SceneData& scene, World& /*world*/)
{
    for (const auto& entry : entries_) {
        if (EntityData* e = dash::editor::findEntity(scene, entry.entityId))
            dash::editor::setLocalTransform(*e, entry.oldLocal);
    }
    if (!entries_.empty()) scene.modified = true;
}
