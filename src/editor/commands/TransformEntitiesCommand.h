#pragma once
#include "ICommand.h"
#include "EntityHierarchy.h"
#include "SceneData.h"

#include <cstdint>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// TransformEntitiesCommand — one undo entry for a whole gizmo drag
//
// The viewport updates transforms live while dragging; on release it hands the
// before/after pairs of every affected entity to this command.
// ─────────────────────────────────────────────────────────────────────────────
class TransformEntitiesCommand : public ICommand {
public:
    struct Entry {
        uint64_t                  entityId = 0;
        dash::editor::Transform3D oldLocal{};
        dash::editor::Transform3D newLocal{};
    };

    TransformEntitiesCommand(std::vector<Entry> entries, const char* label);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return label_; }

    bool empty() const { return entries_.empty(); }

private:
    std::vector<Entry> entries_;
    const char*        label_;
};
