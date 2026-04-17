#pragma once

namespace loopa {

// Reversible action. Every destructive user-visible operation on a track
// (Record New, Overdub, Replace, Clear Loop) is represented as a Command so
// it can be placed on the UndoStack.
class Command {
public:
    virtual ~Command() = default;

    // Apply the change. Called when the user performs the action and again on
    // redo. Implementations must be idempotent enough to run twice (once on
    // initial apply, once on redo after undo).
    virtual void apply() = 0;

    // Reverse the change previously made by apply(). Called on undo.
    virtual void undo() = 0;
};

}  // namespace loopa
