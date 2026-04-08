#pragma once
#include "ICommand.h"
#include "SceneData.h"

// ─────────────────────────────────────────────────────────────────────────────
// MoveEntityCommand – move an entity to a new world position (undoable)
// ─────────────────────────────────────────────────────────────────────────────
class MoveEntityCommand : public ICommand {
public:
    MoveEntityCommand(uint64_t entityId,
                      float oldX, float oldY,
                      float newX, float newY);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return "Move Entity"; }

private:
    uint64_t entityId_;
    float    oldX_, oldY_;
    float    newX_, newY_;
};
