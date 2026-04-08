#include "AddComponentCommand.h"
#include "World.h"
#include <algorithm>

AddComponentCommand::AddComponentCommand(uint64_t entityId, ComponentVariant comp)
    : entityId_(entityId)
    , compType_(getVariantType(comp))
    , comp_(std::move(comp))
{}

void AddComponentCommand::apply(SceneData& scene, World& /*world*/)
{
    for (auto& e : scene.entities) {
        if (e.id != entityId_) continue;
        for (auto& c : e.components)
            if (getVariantType(c) == compType_) return; // already present
        e.components.push_back(comp_);
        break;
    }
    scene.modified = true;
}

void AddComponentCommand::undo(SceneData& scene, World& /*world*/)
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
