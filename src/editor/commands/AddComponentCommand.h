#pragma once
#include "ICommand.h"
#include "SceneData.h"
#include "Reflection.h"

// ─────────────────────────────────────────────────────────────────────────────
// AddComponentCommand — add a component to an entity (undoable)
// ─────────────────────────────────────────────────────────────────────────────
class AddComponentCommand : public ICommand {
public:
    AddComponentCommand(uint64_t entityId, ComponentVariant comp);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return "Add Component"; }

private:
    uint64_t         entityId_;
    ComponentType    compType_;
    ComponentVariant comp_;
};
