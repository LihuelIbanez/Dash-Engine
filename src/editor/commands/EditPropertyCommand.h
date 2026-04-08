#pragma once
#include "ICommand.h"
#include "SceneData.h"
#include "Reflection.h"

// PropertyValue is provided by Reflection.h
// (std::variant<int, float, std::string, bool>)

// ─────────────────────────────────────────────────────────────────────────────
// EditPropertyCommand – edit one named property of an entity (undoable)
// ─────────────────────────────────────────────────────────────────────────────

enum class PropertyTarget {
    Name,
    CharClass,
    PosX,
    PosY,
};

// PropertyValue is defined in Reflection.h (included above)

class EditPropertyCommand : public ICommand {
public:
    EditPropertyCommand(uint64_t      entityId,
                        PropertyTarget target,
                        PropertyValue  oldVal,
                        PropertyValue  newVal);

    void        apply(SceneData& scene, World& world) override;
    void        undo (SceneData& scene, World& world) override;
    const char* name () const override;

private:
    uint64_t       entityId_;
    PropertyTarget target_;
    PropertyValue  oldVal_;
    PropertyValue  newVal_;
    mutable std::string nameCache_;

    void applyValue(EntityData& e, const PropertyValue& val) const;
};
