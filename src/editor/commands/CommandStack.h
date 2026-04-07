#pragma once
#include "ICommand.h"
#include <memory>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// CommandStack – manages undo / redo history for scene commands
// ─────────────────────────────────────────────────────────────────────────────
class CommandStack {
public:
    void execute(std::unique_ptr<ICommand> cmd, SceneData& scene, World& world);
    void undo(SceneData& scene, World& world);
    void redo(SceneData& scene, World& world);
    void clear();

    bool canUndo() const;
    bool canRedo() const;

    const char* undoName() const;
    const char* redoName() const;

private:
    static constexpr std::size_t kMaxHistory = 200;

    std::vector<std::unique_ptr<ICommand>> undoStack_;
    std::vector<std::unique_ptr<ICommand>> redoStack_;
};
