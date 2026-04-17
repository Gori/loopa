#include <catch2/catch_test_macros.hpp>

#include "core/BpmDetector.h"

using loopa::detectBpm;
using loopa::samplesForBars;

namespace {

constexpr double kSR = 48000.0;

// Returns the number of samples for `bars` bars of `beatsPerBar` beats at `bpm`.
std::size_t samplesAt(int bars, double bpm, int beatsPerBar = 4) {
    return samplesForBars(bars, bpm, kSR, beatsPerBar);
}

}  // namespace

TEST_CASE("detectBpm returns exact fits for common tempos", "[bpm]") {
    SECTION("120 BPM, 4 bars") {
        auto fit = detectBpm(samplesAt(4, 120.0), kSR);
        REQUIRE(fit.has_value());
        REQUIRE(fit->bars == 4);
        REQUIRE(fit->bpm == 120.00);
    }

    SECTION("100 BPM, 2 bars") {
        auto fit = detectBpm(samplesAt(2, 100.0), kSR);
        REQUIRE(fit.has_value());
        REQUIRE(fit->bars == 2);
        REQUIRE(fit->bpm == 100.00);
    }

    SECTION("80 BPM, 1 bar") {
        auto fit = detectBpm(samplesAt(1, 80.0), kSR);
        REQUIRE(fit.has_value());
        REQUIRE(fit->bars == 1);
        REQUIRE(fit->bpm == 80.00);
    }
}

TEST_CASE("detectBpm prefers the largest bars when multiple fits are valid", "[bpm]") {
    // 4 seconds at 48 kHz:
    //   bars=1 -> bpm=60   (valid)
    //   bars=2 -> bpm=120  (valid)
    //   bars=4 -> bpm=240  (rejected)
    // Expect: bars=2, bpm=120 (largest valid bars).
    const std::size_t fourSeconds = static_cast<std::size_t>(4.0 * kSR);
    auto fit = detectBpm(fourSeconds, kSR);
    REQUIRE(fit.has_value());
    REQUIRE(fit->bars == 2);
    REQUIRE(fit->bpm == 120.00);
}

TEST_CASE("detectBpm rejects recordings shorter than 1 bar at maxBpm", "[bpm]") {
    // 1 second at 48 kHz -> bars=1 gives bpm=240 (rejected), bars=2 gives 480, etc.
    auto fit = detectBpm(static_cast<std::size_t>(1.0 * kSR), kSR);
    REQUIRE_FALSE(fit.has_value());
}

TEST_CASE("detectBpm rejects recordings longer than 16 bars at minBpm", "[bpm]") {
    // 16 bars at 60 BPM = 64 seconds. Anything longer rejects.
    const std::size_t justTooLong = samplesForBars(16, 60.0, kSR) + 10000;
    auto fit = detectBpm(justTooLong, kSR);
    REQUIRE_FALSE(fit.has_value());
}

TEST_CASE("detectBpm handles zero input without crashing", "[bpm]") {
    REQUIRE_FALSE(detectBpm(0, kSR).has_value());
    REQUIRE_FALSE(detectBpm(48000, 0.0).has_value());
}

TEST_CASE("detectBpm rounds BPM to 2 decimals", "[bpm]") {
    // Pick a fractional length that produces a non-integer BPM.
    // 2 seconds * 48000 samples = 96000. Trim by 1 sample -> slightly faster tempo.
    const std::size_t n = samplesAt(4, 120.0) - 1;
    auto fit = detectBpm(n, kSR);
    REQUIRE(fit.has_value());
    // Verify the value has at most 2 decimals.
    const double scaled = fit->bpm * 100.0;
    REQUIRE(scaled == static_cast<double>(static_cast<long long>(scaled + 0.5)));
}

TEST_CASE("samplesForBars round-trips with detectBpm", "[bpm]") {
    for (int bars : {1, 2, 4, 8, 16}) {
        for (double bpm : {60.0, 85.0, 100.0, 120.0, 140.0}) {
            const auto n = samplesForBars(bars, bpm, kSR);
            auto fit = detectBpm(n, kSR);
            REQUIRE(fit.has_value());
            // We might get a larger-bars fit; just ensure BPM matches when bars match.
            if (fit->bars == bars) {
                REQUIRE(fit->bpm == bpm);
            }
        }
    }
}
