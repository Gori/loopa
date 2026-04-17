#include <catch2/catch_test_macros.hpp>

#include "core/UndoStack.h"

#include <memory>

using loopa::Command;
using loopa::UndoStack;

namespace {

class CountingCommand : public Command {
public:
    explicit CountingCommand(int& state, int delta) : m_state(state), m_delta(delta) {}
    void apply() override { m_state += m_delta; }
    void undo()  override { m_state -= m_delta; }
private:
    int& m_state;
    int  m_delta;
};

}  // namespace

TEST_CASE("UndoStack pushes, undoes, and redoes a command", "[undo]") {
    int state = 0;
    UndoStack stack;
    auto cmd = std::make_unique<CountingCommand>(state, 5);
    cmd->apply();  // apply happened before push, per contract
    stack.pushApplied(std::move(cmd));
    REQUIRE(state == 5);
    REQUIRE(stack.canUndo());
    REQUIRE_FALSE(stack.canRedo());

    REQUIRE(stack.undo());
    REQUIRE(state == 0);
    REQUIRE(stack.canRedo());

    REQUIRE(stack.redo());
    REQUIRE(state == 5);
}

TEST_CASE("UndoStack push clears redo history", "[undo]") {
    int state = 0;
    UndoStack stack;
    {
        auto cmd = std::make_unique<CountingCommand>(state, 3);
        cmd->apply();
        stack.pushApplied(std::move(cmd));
    }
    stack.undo();
    REQUIRE(stack.canRedo());

    {
        auto cmd = std::make_unique<CountingCommand>(state, 7);
        cmd->apply();
        stack.pushApplied(std::move(cmd));
    }
    REQUIRE_FALSE(stack.canRedo());
    REQUIRE(state == 7);
}

TEST_CASE("UndoStack is bounded by maxDepth", "[undo]") {
    int state = 0;
    UndoStack stack(4);
    for (int i = 1; i <= 10; ++i) {
        auto cmd = std::make_unique<CountingCommand>(state, 1);
        cmd->apply();
        stack.pushApplied(std::move(cmd));
    }
    REQUIRE(stack.undoDepth() == 4);
    REQUIRE(state == 10);

    // Undoing 4 times should pull state down by 4 (oldest commands dropped silently).
    while (stack.canUndo()) { stack.undo(); }
    REQUIRE(state == 6);
}

TEST_CASE("UndoStack undo/redo balances over many operations", "[undo]") {
    int state = 0;
    UndoStack stack;
    for (int i = 1; i <= 5; ++i) {
        auto cmd = std::make_unique<CountingCommand>(state, i);
        cmd->apply();
        stack.pushApplied(std::move(cmd));
    }
    REQUIRE(state == 1 + 2 + 3 + 4 + 5);

    while (stack.canUndo()) { stack.undo(); }
    REQUIRE(state == 0);

    while (stack.canRedo()) { stack.redo(); }
    REQUIRE(state == 15);
}

TEST_CASE("UndoStack clear drops both stacks", "[undo]") {
    int state = 0;
    UndoStack stack;
    auto cmd = std::make_unique<CountingCommand>(state, 9);
    cmd->apply();
    stack.pushApplied(std::move(cmd));
    stack.clear();
    REQUIRE_FALSE(stack.canUndo());
    REQUIRE_FALSE(stack.canRedo());
}
