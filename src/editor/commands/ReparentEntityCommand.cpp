#include "ReparentEntityCommand.h"
#include "World.h"

using dash::editor::Transform3D;

ReparentEntityCommand::ReparentEntityCommand(uint64_t entityId, uint64_t newParentId,
                                             bool keepWorldTransform)
    : entityId_(entityId)
    , newParentId_(newParentId)
    , keepWorldTransform_(keepWorldTransform)
{}

void ReparentEntityCommand::apply(SceneData& scene, World& /*world*/)
{
    applied_ = false;
    if (!dash::editor::canReparent(scene, entityId_, newParentId_)) return;

    EntityData* child = dash::editor::findEntity(scene, entityId_);
    if (!child) return;

    oldParentId_ = child->parentId;
    oldLocal_    = dash::editor::localTransform(*child);

    const Transform3D world = dash::editor::worldTransform(scene, entityId_);
    child->parentId = newParentId_;

    if (keepWorldTransform_) {
        if (newParentId_ == 0) {
            dash::editor::setLocalTransform(*child, world);
        } else {
            const Transform3D parentWorld = dash::editor::worldTransform(scene, newParentId_);
            dash::editor::setLocalTransform(
                *child, dash::editor::relativeTransform(parentWorld, world));
        }
    }

    applied_ = true;
    scene.modified = true;
}

void ReparentEntityCommand::undo(SceneData& scene, World& /*world*/)
{
    if (!applied_) return;
    EntityData* child = dash::editor::findEntity(scene, entityId_);
    if (!child) return;

    child->parentId = oldParentId_;
    dash::editor::setLocalTransform(*child, oldLocal_);
    scene.modified = true;
}
