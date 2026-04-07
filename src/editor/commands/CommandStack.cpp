#include "CommandStack.h"

void CommandStack::execute(std::unique_ptr<ICommand> cmd,
                           SceneData& scene, World& world)
{
    cmd->apply(scene, world);
    undoStack_.push_back(std::move(cmd));
    if (undoStack_.size() > kMaxHistory)
        undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
}

void CommandStack::undo(SceneData& scene, World& world)
{
    if (undoStack_.empty()) return;
    auto cmd = std::move(undoStack_.back());
    undoStack_.pop_back();
    cmd->undo(scene, world);
    redoStack_.push_back(std::move(cmd));
}

void CommandStack::redo(SceneData& scene, World& world)
{
    if (redoStack_.empty()) return;
    auto cmd = std::move(redoStack_.back());
    redoStack_.pop_back();
    cmd->apply(scene, world);
    undoStack_.push_back(std::move(cmd));
}

void CommandStack::clear()
{
    undoStack_.clear();
    redoStack_.clear();
}

bool CommandStack::canUndo() const { return !undoStack_.empty(); }
bool CommandStack::canRedo() const { return !redoStack_.empty(); }

const char* CommandStack::undoName() const
{
    return undoStack_.empty() ? nullptr : undoStack_.back()->name();
}

const char* CommandStack::redoName() const
{
    return redoStack_.empty() ? nullptr : redoStack_.back()->name();
}
