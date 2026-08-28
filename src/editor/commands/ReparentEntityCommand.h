#pragma once
#include "ICommand.h"
#include "EntityHierarchy.h"
#include "SceneData.h"

// ─────────────────────────────────────────────────────────────────────────────
// ReparentEntityCommand — change EntityData::parentId (undoable)
//
// The entity keeps its world transform: the local transform is recomputed
// against the new parent. Cycles are rejected, so a no-op apply is possible.
// ─────────────────────────────────────────────────────────────────────────────
class ReparentEntityCommand : public ICommand {
public:
    ReparentEntityCommand(uint64_t entityId, uint64_t newParentId,
                          bool keepWorldTransform = true);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return "Reparent Entity"; }

private:
    uint64_t entityId_;
    uint64_t newParentId_;
    bool     keepWorldTransform_;

    uint64_t                  oldParentId_ = 0;
    dash::editor::Transform3D oldLocal_{};
    bool                      applied_ = false;
};
