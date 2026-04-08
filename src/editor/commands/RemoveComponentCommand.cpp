#include "RemoveComponentCommand.h"
#include "World.h"
#include <algorithm>

RemoveComponentCommand::RemoveComponentCommand(uint64_t entityId, ComponentVariant removedComp)
    : entityId_(entityId)
    , compType_(getVariantType(removedComp))
    , removedComp_(std::move(removedComp))
{}

void RemoveComponentCommand::apply(SceneData& scene, World& /*world*/)
{
    for (auto& e : scene.entities) {
        if (e.id != entityId_) continue;
        auto& comps = e.components;
        comps.erase(
            std::remove_if(comps.begin(), comps.end(),
                [this](const ComponentVariant& c){ return getVariantType(c) == compType_; }),
            comps.end());
        break;
    }
    scene.modified = true;
}

void RemoveComponentCommand::undo(SceneData& scene, World& /*world*/)
{
    for (auto& e : scene.entities) {
        if (e.id != entityId_) continue;
        for (auto& c : e.components)
            if (getVariantType(c) == compType_) return; // already restored
        e.components.push_back(removedComp_);
        break;
    }
    scene.modified = true;
}
