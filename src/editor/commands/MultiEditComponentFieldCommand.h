#pragma once
#include "ICommand.h"
#include "Reflection.h"
#include "SceneData.h"

#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// MultiEditComponentFieldCommand — set one component field on N entities
//
// Same reflection-by-offset mechanism as EditComponentFieldCommand, but the
// whole multi-selection edit collapses into a single undo entry.
// ─────────────────────────────────────────────────────────────────────────────
class MultiEditComponentFieldCommand : public ICommand {
public:
    struct Target {
        uint64_t      entityId = 0;
        PropertyValue oldVal;
    };

    MultiEditComponentFieldCommand(std::vector<Target> targets,
                                   ComponentType compType,
                                   std::size_t   fieldOffset,
                                   PropertyType  fieldType,
                                   PropertyValue newVal,
                                   const std::string& fieldName);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override { return nameCache_.c_str(); }

    std::size_t targetCount() const { return targets_.size(); }

private:
    void writeTo(SceneData& scene, uint64_t entityId, const PropertyValue& val) const;

    std::vector<Target> targets_;
    ComponentType       compType_;
    std::size_t         fieldOffset_;
    PropertyType        fieldType_;
    PropertyValue       newVal_;
    std::string         nameCache_;
};
