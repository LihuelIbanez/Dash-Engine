#include "EraseCommand.h"
#include "World.h"

EraseCommand::EraseCommand(uint64_t entityId)
    : entityId_(entityId)
{}

void EraseCommand::apply(SceneData& scene, World& /*world*/)
{
    for (int i = 0; i < static_cast<int>(scene.entities.size()); ++i) {
        if (scene.entities[i].id == entityId_) {
            if (!captured_) {
                backup_      = scene.entities[i];
                insertIndex_ = i;
                captured_    = true;
            }
            scene.entities.erase(scene.entities.begin() + i);
            scene.modified = true;
            return;
        }
    }
}

void EraseCommand::undo(SceneData& scene, World& /*world*/)
{
    if (!captured_) return;
    int pos = (insertIndex_ >= 0 &&
               insertIndex_ <= static_cast<int>(scene.entities.size()))
                  ? insertIndex_
                  : static_cast<int>(scene.entities.size());
    scene.entities.insert(scene.entities.begin() + pos, backup_);
    scene.modified = true;
}
