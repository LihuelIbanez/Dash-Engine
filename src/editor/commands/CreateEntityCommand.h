#pragma once
#include "ICommand.h"
#include "SceneData.h"

// ─────────────────────────────────────────────────────────────────────────────
// CreateEntityCommand — insert a fully-formed entity (undoable)
//
// Used by the light-creation menu, where the component set is decided by the
// caller instead of being hardcoded like in PlaceEnemyCommand.
// ─────────────────────────────────────────────────────────────────────────────
class CreateEntityCommand : public ICommand {
public:
    CreateEntityCommand(EntityData entity, const char* label = "Create Entity");

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return label_; }

    uint64_t entityId() const { return entity_.id; }

private:
    EntityData  entity_;
    const char* label_;
};
