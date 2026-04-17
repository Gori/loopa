#include <catch2/catch_test_macros.hpp>

#include "core/Transport.h"

using loopa::Transport;

namespace {

Transport make120Bpm4Bars() {
    Transport t;
    t.setTimebase({120.0, 48000.0, 4});   // 2 s/bar, 0.5 s/beat -> 24000 samples/beat
    t.setTotalLengthSamples(24000 * 4 * 4); // 4 bars
    return t;
}

}  // namespace

TEST_CASE("Transport computes samplesPerBeat and samplesPerBar from timebase", "[transport]") {
    Transport t;
    t.setTimebase({120.0, 48000.0, 4});
    REQUIRE(t.samplesPerBeat() == 24000);
    REQUIRE(t.samplesPerBar() == 96000);
}

TEST_CASE("Transport returns zero timing when BPM or SR is invalid", "[transport]") {
    Transport t;
    REQUIRE(t.samplesPerBeat() == 0);
    REQUIRE(t.samplesPerBar() == 0);
    REQUIRE(t.currentBar() == 0);
    REQUIRE(t.currentBeat() == 0);
    REQUIRE(t.totalBars() == 0);
}

TEST_CASE("Transport advances position only when playing", "[transport]") {
    auto t = make120Bpm4Bars();
    t.advance(10000);
    REQUIRE(t.samplePosition() == 0);  // not playing

    t.setPlaying(true);
    t.advance(10000);
    REQUIRE(t.samplePosition() == 10000);
}

TEST_CASE("Transport wraps position at totalLengthSamples", "[transport]") {
    auto t = make120Bpm4Bars();
    t.setPlaying(true);
    // 4 bars * 96000 = 384000 samples total.
    t.advance(380000);
    REQUIRE(t.samplePosition() == 380000);
    t.advance(10000);
    REQUIRE(t.samplePosition() == 6000);  // wrapped
}

TEST_CASE("Transport bar/beat reflect position", "[transport]") {
    auto t = make120Bpm4Bars();
    t.setPlaying(true);
    REQUIRE(t.currentBar() == 0);
    REQUIRE(t.currentBeat() == 0);
    REQUIRE(t.totalBars() == 4);

    t.advance(24000);  // one beat
    REQUIRE(t.currentBar() == 0);
    REQUIRE(t.currentBeat() == 1);

    t.advance(24000 * 3);  // bar boundary
    REQUIRE(t.currentBar() == 1);
    REQUIRE(t.currentBeat() == 0);

    t.advance(96000 * 2);  // two more bars
    REQUIRE(t.currentBar() == 3);
    REQUIRE(t.currentBeat() == 0);
}

TEST_CASE("Transport resetPosition returns to zero", "[transport]") {
    auto t = make120Bpm4Bars();
    t.setPlaying(true);
    t.advance(50000);
    t.resetPosition();
    REQUIRE(t.samplePosition() == 0);
    REQUIRE(t.currentBar() == 0);
}

TEST_CASE("Transport setTotalLengthSamples clamps current position", "[transport]") {
    Transport t;
    t.setTimebase({120.0, 48000.0, 4});
    t.setTotalLengthSamples(200000);
    t.setPlaying(true);
    t.advance(150000);
    REQUIRE(t.samplePosition() == 150000);

    // Shrinking total length wraps the position so it stays valid.
    t.setTotalLengthSamples(100000);
    REQUIRE(t.samplePosition() == 50000);
}

TEST_CASE("Transport never advances when total length is zero", "[transport]") {
    Transport t;
    t.setTimebase({120.0, 48000.0, 4});
    t.setPlaying(true);
    t.advance(1000);
    REQUIRE(t.samplePosition() == 0);
}
