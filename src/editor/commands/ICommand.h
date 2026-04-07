#pragma once

struct SceneData;
class World;

// ─────────────────────────────────────────────────────────────────────────────
// ICommand – interface for reversible scene operations
// ─────────────────────────────────────────────────────────────────────────────
class ICommand {
public:
    virtual ~ICommand() = default;

    virtual void        apply(SceneData& scene, World& world) = 0;
    virtual void        undo (SceneData& scene, World& world) = 0;
    virtual const char* name () const = 0;
};
