#pragma once

#include "core/Command.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace loopa {

// Bounded undo/redo stack. Push a Command after applying it. Undo pops from
// the undo side and pushes onto the redo side; redo does the inverse. Pushing
// a new command clears the redo history.
class UndoStack {
public:
    explicit UndoStack(std::size_t maxDepth = 64) : m_maxDepth(maxDepth) {
        m_undo.reserve(maxDepth);
        m_redo.reserve(maxDepth);
    }

    void pushApplied(std::unique_ptr<Command> cmd) {
        if (cmd == nullptr) {
            return;
        }
        m_redo.clear();
        if (m_undo.size() >= m_maxDepth) {
            m_undo.erase(m_undo.begin());
        }
        m_undo.push_back(std::move(cmd));
    }

    bool canUndo() const noexcept { return !m_undo.empty(); }
    bool canRedo() const noexcept { return !m_redo.empty(); }

    std::size_t undoDepth() const noexcept { return m_undo.size(); }
    std::size_t redoDepth() const noexcept { return m_redo.size(); }

    bool undo() {
        if (m_undo.empty()) {
            return false;
        }
        auto cmd = std::move(m_undo.back());
        m_undo.pop_back();
        cmd->undo();
        m_redo.push_back(std::move(cmd));
        return true;
    }

    bool redo() {
        if (m_redo.empty()) {
            return false;
        }
        auto cmd = std::move(m_redo.back());
        m_redo.pop_back();
        cmd->apply();
        m_undo.push_back(std::move(cmd));
        return true;
    }

    void clear() noexcept {
        m_undo.clear();
        m_redo.clear();
    }

    std::size_t maxDepth() const noexcept { return m_maxDepth; }

private:
    std::vector<std::unique_ptr<Command>> m_undo;
    std::vector<std::unique_ptr<Command>> m_redo;
    std::size_t m_maxDepth;
};

}  // namespace loopa
