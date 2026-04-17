#include <catch2/catch_test_macros.hpp>

#include "core/Metronome.h"

#include <vector>

using loopa::Metronome;

TEST_CASE("Metronome is inactive before trigger", "[metronome]") {
    Metronome m;
    m.prepareToPlay(48000.0);
    REQUIRE_FALSE(m.isActive());

    std::vector<float> buf(128, 0.0f);
    m.render(buf.data(), 128);
    for (float x : buf) {
        REQUIRE(x == 0.0f);
    }
}

TEST_CASE("Metronome generates audio after trigger", "[metronome]") {
    Metronome m;
    m.prepareToPlay(48000.0);
    m.trigger(0, false);
    REQUIRE(m.isActive());

    std::vector<float> buf(256, 0.0f);
    m.render(buf.data(), 256);

    bool anyNonZero = false;
    for (float x : buf) {
        if (x != 0.0f) { anyNonZero = true; break; }
    }
    REQUIRE(anyNonZero);
}

TEST_CASE("Metronome respects offsetSamples within the block", "[metronome]") {
    Metronome m;
    m.prepareToPlay(48000.0);
    m.trigger(64, false);

    std::vector<float> buf(128, 0.0f);
    m.render(buf.data(), 128);

    for (int i = 0; i < 64; ++i) {
        REQUIRE(buf[static_cast<std::size_t>(i)] == 0.0f);
    }
    bool anyAfter = false;
    for (int i = 64; i < 128; ++i) {
        if (buf[static_cast<std::size_t>(i)] != 0.0f) { anyAfter = true; break; }
    }
    REQUIRE(anyAfter);
}

TEST_CASE("Metronome downbeat click differs from plain beat click", "[metronome]") {
    Metronome mBeat;
    mBeat.prepareToPlay(48000.0);
    mBeat.trigger(0, false);
    std::vector<float> bBeat(256, 0.0f);
    mBeat.render(bBeat.data(), 256);

    Metronome mDown;
    mDown.prepareToPlay(48000.0);
    mDown.trigger(0, true);
    std::vector<float> bDown(256, 0.0f);
    mDown.render(bDown.data(), 256);

    bool different = false;
    for (std::size_t i = 0; i < 256; ++i) {
        if (bBeat[i] != bDown[i]) { different = true; break; }
    }
    REQUIRE(different);
}

TEST_CASE("Metronome completes and goes idle", "[metronome]") {
    Metronome m;
    m.prepareToPlay(48000.0);
    m.trigger(0, false);

    // A 30 ms click at 48 kHz is ~1440 samples — render enough to finish it.
    std::vector<float> buf(2048, 0.0f);
    m.render(buf.data(), 2048);
    REQUIRE_FALSE(m.isActive());
}
