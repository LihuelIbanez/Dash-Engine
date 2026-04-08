#pragma once
#include "Components.h"
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// EntityRegistry — runtime store mapping entity IDs to component vectors
// ─────────────────────────────────────────────────────────────────────────────
class EntityRegistry {
public:
    // ── Lifecycle ────────────────────────────────────────────────────────────

    /// Allocate a new entity ID and register it; returns the new ID.
    uint64_t createEntity();

    /// Remove an entity and all its components.
    void destroyEntity(uint64_t id);

    /// Remove all entities and components; reset ID counter.
    void clear();

    // ── Component access (template — defined inline) ──────────────────────────

    /// Add a default-constructed component of type T.
    /// If the entity already has this component, returns a reference to it.
    template<typename T>
    T& addComponent(uint64_t id)
    {
        auto& comps = storage_[id];
        for (auto& c : comps) {
            if (std::holds_alternative<T>(c))
                return std::get<T>(c);
        }
        comps.emplace_back(T{});
        // Track entity in the ID list if first component added
        if (std::find(ids_.begin(), ids_.end(), id) == ids_.end())
            ids_.push_back(id);
        return std::get<T>(comps.back());
    }

    /// Return pointer to T component, or nullptr if entity/component absent.
    template<typename T>
    T* getComponent(uint64_t id)
    {
        auto it = storage_.find(id);
        if (it == storage_.end()) return nullptr;
        for (auto& c : it->second) {
            if (std::holds_alternative<T>(c))
                return &std::get<T>(c);
        }
        return nullptr;
    }

    template<typename T>
    const T* getComponent(uint64_t id) const
    {
        auto it = storage_.find(id);
        if (it == storage_.end()) return nullptr;
        for (const auto& c : it->second) {
            if (std::holds_alternative<T>(c))
                return &std::get<T>(c);
        }
        return nullptr;
    }

    /// Return true if the entity has a component of type T.
    template<typename T>
    bool hasComponent(uint64_t id) const
    {
        auto it = storage_.find(id);
        if (it == storage_.end()) return false;
        for (const auto& c : it->second) {
            if (std::holds_alternative<T>(c))
                return true;
        }
        return false;
    }

    /// Remove the T component from the entity (no-op if absent).
    template<typename T>
    void removeComponent(uint64_t id)
    {
        auto it = storage_.find(id);
        if (it == storage_.end()) return;
        auto& comps = it->second;
        comps.erase(
            std::remove_if(comps.begin(), comps.end(),
                [](const ComponentVariant& c){ return std::holds_alternative<T>(c); }),
            comps.end());
    }

    // ── Non-template accessors (defined in .cpp) ──────────────────────────────

    /// Direct access to the full component vector for an entity.
    /// Creates an empty entry if the entity is not yet in storage.
    std::vector<ComponentVariant>& getComponents(uint64_t id);

    /// Ordered list of all known entity IDs.
    const std::vector<uint64_t>& allEntities() const;

private:
    std::unordered_map<uint64_t, std::vector<ComponentVariant>> storage_;
    std::vector<uint64_t> ids_;
    uint64_t nextId_ = 1;
};
