#include <catch2/catch_test_macros.hpp>

#include "core/MidiMap.h"

using loopa::MidiAction;
using loopa::MidiMap;
using loopa::MidiTrigger;

namespace {

MidiTrigger cc(int ch, int num)   { return MidiTrigger{MidiTrigger::Kind::Cc, ch, num}; }
MidiTrigger note(int ch, int num) { return MidiTrigger{MidiTrigger::Kind::Note, ch, num}; }

}  // namespace

TEST_CASE("MidiMap starts empty and returns no match", "[midimap]") {
    MidiMap m;
    REQUIRE_FALSE(m.get(MidiAction::StartStopRecord).valid());
    REQUIRE_FALSE(m.match(cc(0, 60)).has_value());
}

TEST_CASE("MidiMap bind and match round-trip", "[midimap]") {
    MidiMap m;
    m.bind(MidiAction::StartStopRecord, cc(0, 64));
    m.bind(MidiAction::CycleNext,       note(1, 36));
    m.bind(MidiAction::CyclePrev,       note(1, 37));
    m.bind(MidiAction::MuteSelected,    cc(2, 10));
    m.bind(MidiAction::NextLoopSelected, cc(2, 11));

    REQUIRE(m.match(cc(0, 64))   == MidiAction::StartStopRecord);
    REQUIRE(m.match(note(1, 36)) == MidiAction::CycleNext);
    REQUIRE(m.match(note(1, 37)) == MidiAction::CyclePrev);
    REQUIRE(m.match(cc(2, 10))   == MidiAction::MuteSelected);
    REQUIRE(m.match(cc(2, 11))   == MidiAction::NextLoopSelected);
    REQUIRE_FALSE(m.match(cc(2, 12)).has_value());
}

TEST_CASE("MidiMap last-wins when the same trigger is bound twice", "[midimap]") {
    MidiMap m;
    m.bind(MidiAction::StartStopRecord, cc(0, 64));
    m.bind(MidiAction::CycleNext,       cc(0, 64));  // same trigger

    REQUIRE_FALSE(m.get(MidiAction::StartStopRecord).valid());  // displaced
    REQUIRE(m.get(MidiAction::CycleNext).equals(cc(0, 64)));
    REQUIRE(m.match(cc(0, 64)) == MidiAction::CycleNext);
}

TEST_CASE("MidiMap unbind removes a specific action", "[midimap]") {
    MidiMap m;
    m.bind(MidiAction::StartStopRecord, cc(0, 64));
    m.bind(MidiAction::CycleNext,       note(1, 36));
    m.unbind(MidiAction::StartStopRecord);
    REQUIRE_FALSE(m.get(MidiAction::StartStopRecord).valid());
    REQUIRE(m.get(MidiAction::CycleNext).valid());
    REQUIRE_FALSE(m.match(cc(0, 64)).has_value());
}

TEST_CASE("MidiMap distinguishes Note and CC with the same number", "[midimap]") {
    MidiMap m;
    m.bind(MidiAction::StartStopRecord, cc(0, 60));
    m.bind(MidiAction::CycleNext,       note(0, 60));
    REQUIRE(m.match(cc(0, 60))   == MidiAction::StartStopRecord);
    REQUIRE(m.match(note(0, 60)) == MidiAction::CycleNext);
}

TEST_CASE("MidiMap clear empties every binding", "[midimap]") {
    MidiMap m;
    m.bind(MidiAction::StartStopRecord, cc(0, 64));
    m.bind(MidiAction::MuteSelected,    note(1, 36));
    m.clear();
    for (const auto& t : m.triggers()) {
        REQUIRE_FALSE(t.valid());
    }
}
