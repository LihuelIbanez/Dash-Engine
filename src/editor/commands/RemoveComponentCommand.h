#pragma once
#include "ICommand.h"
#include "SceneData.h"
#include "Reflection.h"

// ─────────────────────────────────────────────────────────────────────────────
// RemoveComponentCommand — remove a component from an entity (undoable).
// Stores the removed component so undo can restore it exactly.
// ─────────────────────────────────────────────────────────────────────────────
class RemoveComponentCommand : public ICommand {
public:
    RemoveComponentCommand(uint64_t entityId, ComponentVariant removedComp);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return "Remove Component"; }

private:
    uint64_t         entityId_;
    ComponentType    compType_;
    ComponentVariant removedComp_;
};
