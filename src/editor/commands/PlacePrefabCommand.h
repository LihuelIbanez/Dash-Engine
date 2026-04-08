#pragma once
#include "ICommand.h"
#include "SceneData.h"
#include "Components.h"
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// PlacePrefabCommand – instantiate a prefab at a world position (undo removes it)
// ─────────────────────────────────────────────────────────────────────────────
class PlacePrefabCommand : public ICommand {
public:
    PlacePrefabCommand(float x, float y,
                       uint64_t entityId,
                       const std::string& entityName,
                       const std::string& prefabGuid,
                       std::vector<ComponentVariant> components);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name() const override { return "Place Prefab"; }

    uint64_t entityId() const { return entity_.id; }

private:
    EntityData entity_;
};
