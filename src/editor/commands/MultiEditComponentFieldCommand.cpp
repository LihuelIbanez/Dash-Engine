#include "MultiEditComponentFieldCommand.h"
#include "World.h"

MultiEditComponentFieldCommand::MultiEditComponentFieldCommand(
    std::vector<Target> targets, ComponentType compType, std::size_t fieldOffset,
    PropertyType fieldType, PropertyValue newVal, const std::string& fieldName)
    : targets_(std::move(targets))
    , compType_(compType)
    , fieldOffset_(fieldOffset)
    , fieldType_(fieldType)
    , newVal_(std::move(newVal))
    , nameCache_("Edit " + fieldName + " (" + std::to_string(targets_.size()) + ")")
{}

void MultiEditComponentFieldCommand::writeTo(SceneData& scene, uint64_t entityId,
                                             const PropertyValue& val) const
{
    for (auto& e : scene.entities) {
        if (e.id != entityId) continue;
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
        break;
    }
}

void MultiEditComponentFieldCommand::apply(SceneData& scene, World& /*world*/)
{
    for (const auto& t : targets_)
        writeTo(scene, t.entityId, newVal_);
    if (!targets_.empty()) scene.modified = true;
}

void MultiEditComponentFieldCommand::undo(SceneData& scene, World& /*world*/)
{
    for (const auto& t : targets_)
        writeTo(scene, t.entityId, t.oldVal);
    if (!targets_.empty()) scene.modified = true;
}
