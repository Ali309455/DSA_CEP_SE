// UndoManager.h - the two stacks behind Undo and Redo.
//
//   undo stack : steps that can be reversed (newest on top)
//   redo stack : steps that were undone and can be applied again
//
// Rules:
//   * A new edit is pushed on the undo stack and EMPTIES the redo stack
//     (once you change something new, the old "future" no longer makes sense).
//   * Undo moves the top step from undo to redo. Redo moves it back.
//   * The undo stack is capped (default 100 steps); the oldest step is dropped.
//
// This class only stores steps. Applying them to the text is Editor's job.
#pragma once
#include <cstddef>
#include <string>
#include "Command.h"
#include "Stack.h"

class UndoManager {
public:
    // A new edit was made.
    void record(const Group& g) {
        undo_.push(g);
        redo_.clear();
        undo_.trim(limit_);
    }

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    size_t undoCount() const { return undo_.size(); }
    size_t redoCount() const { return redo_.size(); }

    // The newest undo step, for merging a run of typing into it (see Editor::typeText).
    Group& topUndo() { return undo_.top(); }
    void clearRedo() { redo_.clear(); }

    const std::string& nextUndoLabel() const { return undo_.top().label; }
    const std::string& nextRedoLabel() const { return redo_.top().label; }

    // Undo: take the newest step off the undo stack (caller reverses it, then calls putRedo).
    Group takeUndo() { return undo_.pop(); }
    void putRedo(const Group& g) { redo_.push(g); }

    // Redo: take the newest step off the redo stack (caller re-applies it, then calls putUndo).
    Group takeRedo() { return redo_.pop(); }
    void putUndo(const Group& g) { undo_.push(g); undo_.trim(limit_); }

    void setLimit(size_t n) { limit_ = n < 1 ? 1 : n; undo_.trim(limit_); }
    size_t limit() const { return limit_; }

    void clear() { undo_.clear(); redo_.clear(); }

private:
    Stack<Group> undo_;
    Stack<Group> redo_;
    size_t limit_ = 100;
};
