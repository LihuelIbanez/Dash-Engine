#pragma once
#include "ICommand.h"
#include "SceneData.h"

// ─────────────────────────────────────────────────────────────────────────────
// PlaceEnemyCommand – spawn an enemy entity, removable via undo
// ─────────────────────────────────────────────────────────────────────────────
class PlaceEnemyCommand : public ICommand {
public:
    PlaceEnemyCommand(float x, float y, uint64_t entityId,
                      const std::string& name = "Enemy");

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return "Place Enemy"; }

    uint64_t entityId() const { return entity_.id; }

private:
    EntityData entity_;
};
