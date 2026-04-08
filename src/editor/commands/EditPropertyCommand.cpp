#include "EditPropertyCommand.h"
#include "World.h"

EditPropertyCommand::EditPropertyCommand(uint64_t      entityId,
                                         PropertyTarget target,
                                         PropertyValue  oldVal,
                                         PropertyValue  newVal)
    : entityId_(entityId)
    , target_(target)
    , oldVal_(std::move(oldVal))
    , newVal_(std::move(newVal))
{}

void EditPropertyCommand::applyValue(EntityData& e, const PropertyValue& val) const
{
    switch (target_) {
    case PropertyTarget::Name:
        e.name = std::get<std::string>(val);
        break;
    case PropertyTarget::CharClass:
        e.charClass = std::get<std::string>(val);
        break;
    case PropertyTarget::PosX:
        e.x = std::get<float>(val);
        break;
    case PropertyTarget::PosY:
        e.y = std::get<float>(val);
        break;
    }
}

void EditPropertyCommand::apply(SceneData& scene, World& /*world*/)
{
    for (auto& e : scene.entities) {
        if (e.id == entityId_) {
            applyValue(e, newVal_);
            break;
        }
    }
    scene.modified = true;
}

void EditPropertyCommand::undo(SceneData& scene, World& /*world*/)
{
    for (auto& e : scene.entities) {
        if (e.id == entityId_) {
            applyValue(e, oldVal_);
            break;
        }
    }
    scene.modified = true;
}

const char* EditPropertyCommand::name() const
{
    switch (target_) {
    case PropertyTarget::Name:     nameCache_ = "Edit Name";      break;
    case PropertyTarget::CharClass: nameCache_ = "Edit Class";    break;
    case PropertyTarget::PosX:     nameCache_ = "Edit Position X"; break;
    case PropertyTarget::PosY:     nameCache_ = "Edit Position Y"; break;
    }
    return nameCache_.c_str();
}
