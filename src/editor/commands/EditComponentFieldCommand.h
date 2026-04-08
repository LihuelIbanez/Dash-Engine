#pragma once
#include "ICommand.h"
#include "SceneData.h"
#include "Reflection.h"
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// EditComponentFieldCommand — edit one field of an entity's component (undoable)
// Uses byte-offset reflection to locate the field inside the ComponentVariant.
// ─────────────────────────────────────────────────────────────────────────────
class EditComponentFieldCommand : public ICommand {
public:
    EditComponentFieldCommand(uint64_t       entityId,
                              ComponentType  compType,
                              std::size_t    fieldOffset,
                              PropertyType   fieldType,
                              PropertyValue  oldVal,
                              PropertyValue  newVal,
                              std::string    fieldName);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override;

private:
    uint64_t      entityId_;
    ComponentType compType_;
    std::size_t   fieldOffset_;
    PropertyType  fieldType_;
    PropertyValue oldVal_;
    PropertyValue newVal_;
    mutable std::string nameCache_;

    void applyValue(EntityData& e, const PropertyValue& val) const;
};
