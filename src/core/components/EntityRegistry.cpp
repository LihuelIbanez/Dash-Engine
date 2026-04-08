#include "EntityRegistry.h"
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────

uint64_t EntityRegistry::createEntity()
{
    uint64_t id = nextId_++;
    storage_[id]; // default-construct empty vector
    ids_.push_back(id);
    return id;
}

void EntityRegistry::destroyEntity(uint64_t id)
{
    storage_.erase(id);
    ids_.erase(std::remove(ids_.begin(), ids_.end(), id), ids_.end());
}

void EntityRegistry::clear()
{
    storage_.clear();
    ids_.clear();
    nextId_ = 1;
}

std::vector<ComponentVariant>& EntityRegistry::getComponents(uint64_t id)
{
    auto& comps = storage_[id];
    if (std::find(ids_.begin(), ids_.end(), id) == ids_.end())
        ids_.push_back(id);
    return comps;
}

const std::vector<uint64_t>& EntityRegistry::allEntities() const
{
    return ids_;
}
