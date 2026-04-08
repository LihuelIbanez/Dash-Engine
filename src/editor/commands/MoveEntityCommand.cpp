#include "MoveEntityCommand.h"
#include "World.h"
#include <algorithm>

MoveEntityCommand::MoveEntityCommand(uint64_t entityId,
                                     float oldX, float oldY,
                                     float newX, float newY)
    : entityId_(entityId)
    , oldX_(oldX), oldY_(oldY)
    , newX_(newX), newY_(newY)
{}

static void syncTransform(EntityData& e, float x, float y)
{
    for (auto& comp : e.components) {
        if (auto* tf = std::get_if<TransformComponent>(&comp)) {
            tf->x = x; tf->y = y;
        }
    }
}

void MoveEntityCommand::apply(SceneData& scene, World& /*world*/)
{
    for (auto& e : scene.entities) {
        if (e.id == entityId_) {
            e.x = newX_; e.y = newY_;
            syncTransform(e, newX_, newY_);
            break;
        }
    }
    scene.modified = true;
}

void MoveEntityCommand::undo(SceneData& scene, World& /*world*/)
{
    for (auto& e : scene.entities) {
        if (e.id == entityId_) {
            e.x = oldX_; e.y = oldY_;
            syncTransform(e, oldX_, oldY_);
            break;
        }
    }
    scene.modified = true;
}
