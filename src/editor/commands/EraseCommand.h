#pragma once
#include "ICommand.h"
#include "SceneData.h"

// ─────────────────────────────────────────────────────────────────────────────
// EraseCommand – remove an entity by ID, restorable via undo
// ─────────────────────────────────────────────────────────────────────────────
class EraseCommand : public ICommand {
public:
    explicit EraseCommand(uint64_t entityId);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return "Erase Entity"; }

private:
    uint64_t   entityId_;
    EntityData backup_;        // captured on apply for restore
    int        insertIndex_ = -1;
    bool       captured_    = false;
};
