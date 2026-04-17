#include <catch2/catch_test_macros.hpp>

#include "core/Track.h"

using loopa::Loop;
using loopa::RecordMode;
using loopa::RecordState;
using loopa::Track;
using loopa::isAllowedBarCount;

namespace {

std::shared_ptr<Loop> makeLoop(int bars) {
    return std::make_shared<Loop>(bars, 120.0, 48000.0, 1000u);
}

}  // namespace

TEST_CASE("Track default construction", "[track]") {
    Track t(3);
    REQUIRE(t.id() == 3);
    REQUIRE_FALSE(t.armed());
    REQUIRE_FALSE(t.muted());
    REQUIRE(t.inputCh() == 0);
    REQUIRE(t.bars() == 4);
    REQUIRE(t.mode() == RecordMode::New);
    REQUIRE(t.recordState() == RecordState::Idle);
    REQUIRE(t.loopCount() == 0);
    REQUIRE(t.activeLoopIx() == -1);
    REQUIRE(t.activeLoop() == nullptr);
}

TEST_CASE("isAllowedBarCount matches {1,2,4,8,16}", "[track]") {
    for (int b : {1, 2, 4, 8, 16}) {
        REQUIRE(isAllowedBarCount(b));
    }
    for (int b : {0, 3, 5, 6, 7, 9, 12, 15, 17, 32, -1}) {
        REQUIRE_FALSE(isAllowedBarCount(b));
    }
}

TEST_CASE("Track setBars only accepts allowed values", "[track]") {
    Track t(0);
    t.setBars(2);
    REQUIRE(t.bars() == 2);
    t.setBars(3);           // rejected
    REQUIRE(t.bars() == 2);
    t.setBars(16);
    REQUIRE(t.bars() == 16);
    t.setBars(0);           // rejected
    REQUIRE(t.bars() == 16);
}

TEST_CASE("Track arm/mute toggles", "[track]") {
    Track t(0);
    t.setArmed(true);
    REQUIRE(t.armed());
    t.setMuted(true);
    REQUIRE(t.muted());
    t.setArmed(false);
    t.setMuted(false);
    REQUIRE_FALSE(t.armed());
    REQUIRE_FALSE(t.muted());
}

TEST_CASE("Track addLoop appends and makes it active", "[track]") {
    Track t(0);
    int ix1 = t.addLoop(makeLoop(4));
    REQUIRE(ix1 == 0);
    REQUIRE(t.loopCount() == 1);
    REQUIRE(t.activeLoopIx() == 0);
    REQUIRE(t.activeLoop() != nullptr);
    REQUIRE(t.activeLoop()->bars() == 4);

    int ix2 = t.addLoop(makeLoop(8));
    REQUIRE(ix2 == 1);
    REQUIRE(t.activeLoopIx() == 1);
    REQUIRE(t.activeLoop()->bars() == 8);
}

TEST_CASE("Track removeLoop updates active index sensibly", "[track]") {
    Track t(0);
    t.addLoop(makeLoop(1));
    t.addLoop(makeLoop(2));
    t.addLoop(makeLoop(4));
    t.setActiveLoopIx(0);
    REQUIRE(t.activeLoop()->bars() == 1);

    // Remove the middle one — active still 0
    t.removeLoop(1);
    REQUIRE(t.loopCount() == 2);
    REQUIRE(t.activeLoopIx() == 0);
    REQUIRE(t.activeLoop()->bars() == 1);

    // Remove the first — active 0 points at the remaining one
    t.removeLoop(0);
    REQUIRE(t.loopCount() == 1);
    REQUIRE(t.activeLoopIx() == 0);
    REQUIRE(t.activeLoop()->bars() == 4);

    t.removeLoop(0);
    REQUIRE(t.loopCount() == 0);
    REQUIRE(t.activeLoopIx() == -1);
    REQUIRE(t.activeLoop() == nullptr);
}

TEST_CASE("Track cycleNextLoop wraps", "[track]") {
    Track t(0);
    t.addLoop(makeLoop(1));
    t.addLoop(makeLoop(2));
    t.addLoop(makeLoop(4));
    t.setActiveLoopIx(0);
    t.cycleNextLoop();
    REQUIRE(t.activeLoopIx() == 1);
    t.cycleNextLoop();
    REQUIRE(t.activeLoopIx() == 2);
    t.cycleNextLoop();
    REQUIRE(t.activeLoopIx() == 0);
}

TEST_CASE("Track replaceActiveLoop swaps the current pointer only", "[track]") {
    Track t(0);
    t.addLoop(makeLoop(4));
    t.addLoop(makeLoop(8));
    t.setActiveLoopIx(0);

    auto newLoop = makeLoop(2);
    t.replaceActiveLoop(newLoop);
    REQUIRE(t.activeLoopIx() == 0);
    REQUIRE(t.activeLoop()->bars() == 2);
    REQUIRE(t.loopAt(1)->bars() == 8);  // other loop untouched
}

TEST_CASE("Track record state transitions", "[track]") {
    Track t(0);
    REQUIRE(t.recordState() == RecordState::Idle);
    t.setRecordState(RecordState::CountIn);
    REQUIRE(t.recordState() == RecordState::CountIn);
    t.setRecordState(RecordState::Recording);
    REQUIRE(t.recordState() == RecordState::Recording);
    t.setRecordState(RecordState::Idle);
    REQUIRE(t.recordState() == RecordState::Idle);
}
