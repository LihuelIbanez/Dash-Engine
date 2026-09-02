#pragma once
#include "ICommand.h"
#include "SceneData.h"
#include "Components.h"

#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// GenerateSettlementsCommand – bulk-inserts the buildings and road tile
// overrides produced by SettlementPanel/generateSettlements() as a single
// undoable step.
// ─────────────────────────────────────────────────────────────────────────────
class GenerateSettlementsCommand : public ICommand {
public:
    struct Building {
        uint64_t                      entityId = 0;
        std::string                   name;
        float                         x = 0.f;
        float                         y = 0.f;
        std::string                   prefabGuid;
        std::vector<ComponentVariant> components;
    };

    GenerateSettlementsCommand(std::vector<Building> buildings,
                              std::vector<TileOverride> roadTiles);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name() const override { return "Generate Settlements"; }

private:
    std::vector<Building>     buildings_;
    std::vector<TileOverride> roadTiles_;
};
