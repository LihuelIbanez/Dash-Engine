#include "EditComponentFieldCommand.h"
#include "World.h"

EditComponentFieldCommand::EditComponentFieldCommand(
    uint64_t entityId, ComponentType compType, std::size_t fieldOffset,
    PropertyType fieldType, PropertyValue oldVal, PropertyValue newVal,
    std::string fieldName)
    : entityId_(entityId)
    , compType_(compType)
    , fieldOffset_(fieldOffset)
    , fieldType_(fieldType)
    , oldVal_(std::move(oldVal))
    , newVal_(std::move(newVal))
    , nameCache_("Edit " + fieldName)
{}

void EditComponentFieldCommand::applyValue(EntityData& e, const PropertyValue& val) const
{
    for (auto& comp : e.components) {
        if (getVariantType(comp) != compType_) continue;
        void* ptr = std::visit([offset = fieldOffset_](auto& c) -> void* {
            return reinterpret_cast<char*>(&c) + offset;
        }, comp);
        writeFieldValue(ptr, fieldType_, val);
        // Keep legacy EntityData.x/y in sync with TransformComponent
        if (compType_ == ComponentType::Transform) {
            if (auto* tf = std::get_if<TransformComponent>(&comp)) {
                e.x = tf->x;
                e.y = tf->y;
            }
        }
        break;
    }
}

void EditComponentFieldCommand::apply(SceneData& scene, World& /*world*/)
{
    for (auto& e : scene.entities)
        if (e.id == entityId_) { applyValue(e, newVal_); break; }
    scene.modified = true;
}

void EditComponentFieldCommand::undo(SceneData& scene, World& /*world*/)
{
    for (auto& e : scene.entities)
        if (e.id == entityId_) { applyValue(e, oldVal_); break; }
    scene.modified = true;
}

const char* EditComponentFieldCommand::name() const
{
    return nameCache_.c_str();
}
