#pragma once
#include <functional>
#include <string>

struct SceneData;
class World;
class CommandStack;

// ─────────────────────────────────────────────────────────────────────────────
// SettlementPanel — triggers procedural settlement + road generation
// (world/SettlementGenerator.h) and inserts the result into the current scene
// as a single undoable command.
// ─────────────────────────────────────────────────────────────────────────────
class SettlementPanel {
public:
    using LogCallback = std::function<void(const std::string&)>;

    void draw(SceneData& scene, World& world, CommandStack& commandStack,
              const std::string& assetsRoot, const LogCallback& log);

private:
    int   count_      = 4;
    float minSpacing_ = 24.f;
    int   seed_       = 1;
};
