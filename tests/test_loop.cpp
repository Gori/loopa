#include <catch2/catch_test_macros.hpp>

#include "core/Loop.h"

#include <cmath>
#include <vector>

using loopa::Loop;

namespace {

std::vector<float> ramp(std::size_t n, float start = 0.0f, float step = 0.1f) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = start + step * static_cast<float>(i);
    }
    return v;
}

}  // namespace

TEST_CASE("Loop constructs with the requested length and metadata", "[loop]") {
    Loop l(4, 120.0, 48000.0, 96000);
    REQUIRE(l.bars() == 4);
    REQUIRE(l.bpm() == 120.0);
    REQUIRE(l.sampleRate() == 48000.0);
    REQUIRE(l.length() == 96000);
    REQUIRE_FALSE(l.empty());
    REQUIRE(l.readWrapped(0) == 0.0f);
    REQUIRE(l.readWrapped(96000 - 1) == 0.0f);
}

TEST_CASE("Loop readWrapped wraps around length", "[loop]") {
    Loop l(1, 120.0, 48000.0, 100);
    const auto src = ramp(100, 1.0f, 1.0f);
    l.replaceRegion(src.data(), src.size(), 0);

    REQUIRE(l.readWrapped(0)   == 1.0f);
    REQUIRE(l.readWrapped(99)  == 100.0f);
    REQUIRE(l.readWrapped(100) == 1.0f);
    REQUIRE(l.readWrapped(199) == 100.0f);
    REQUIRE(l.readWrapped(250) == 51.0f);
}

TEST_CASE("Loop overdubSum adds samples and clamps to [-1, 1]", "[loop]") {
    Loop l(1, 120.0, 48000.0, 10);

    const std::vector<float> a(10, 0.3f);
    l.overdubSum(a.data(), a.size(), 0);
    for (std::size_t i = 0; i < 10; ++i) {
        REQUIRE(l.readWrapped(i) == 0.3f);
    }

    const std::vector<float> b(10, 0.4f);
    l.overdubSum(b.data(), b.size(), 0);
    for (std::size_t i = 0; i < 10; ++i) {
        REQUIRE(std::abs(l.readWrapped(i) - 0.7f) < 1e-5f);
    }

    // Push past 1.0 to test clamping.
    const std::vector<float> c(10, 0.5f);
    l.overdubSum(c.data(), c.size(), 0);
    for (std::size_t i = 0; i < 10; ++i) {
        REQUIRE(l.readWrapped(i) == 1.0f);
    }
}

TEST_CASE("Loop replaceRegion writes src over existing audio", "[loop]") {
    Loop l(1, 120.0, 48000.0, 8);
    const std::vector<float> a(8, 0.5f);
    l.replaceRegion(a.data(), a.size(), 0);
    for (std::size_t i = 0; i < 8; ++i) {
        REQUIRE(l.readWrapped(i) == 0.5f);
    }
    const std::vector<float> b = {1.0f, 2.0f, 3.0f, 4.0f};
    l.replaceRegion(b.data(), b.size(), 2);
    REQUIRE(l.readWrapped(0) == 0.5f);
    REQUIRE(l.readWrapped(1) == 0.5f);
    REQUIRE(l.readWrapped(2) == 1.0f);
    REQUIRE(l.readWrapped(5) == 4.0f);
    REQUIRE(l.readWrapped(6) == 0.5f);
}

TEST_CASE("Loop clearAudio zeros the buffer without changing metadata", "[loop]") {
    Loop l(2, 100.0, 48000.0, 16);
    const std::vector<float> a(16, 0.9f);
    l.replaceRegion(a.data(), a.size(), 0);
    l.clearAudio();
    REQUIRE(l.bars() == 2);
    REQUIRE(l.bpm() == 100.0);
    REQUIRE(l.length() == 16);
    for (std::size_t i = 0; i < 16; ++i) {
        REQUIRE(l.readWrapped(i) == 0.0f);
    }
}

TEST_CASE("Loop version increments on every modifying operation", "[loop]") {
    Loop l(1, 120.0, 48000.0, 8);
    const auto v0 = l.version();
    REQUIRE(v0 > 0);

    const std::vector<float> a(8, 0.25f);
    l.replaceRegion(a.data(), a.size(), 0);
    const auto v1 = l.version();
    REQUIRE(v1 > v0);

    const std::vector<float> b(8, 0.25f);
    l.overdubSum(b.data(), b.size(), 0);
    const auto v2 = l.version();
    REQUIRE(v2 > v1);

    l.clearAudio();
    const auto v3 = l.version();
    REQUIRE(v3 > v2);

    // No-op overdub (empty input) should NOT bump the version — nothing changed.
    l.overdubSum(b.data(), 0, 0);
    REQUIRE(l.version() == v3);
}

TEST_CASE("Loop snapshot deep-copies audio", "[loop]") {
    Loop l(1, 120.0, 48000.0, 4);
    const std::vector<float> a = {0.1f, 0.2f, 0.3f, 0.4f};
    l.replaceRegion(a.data(), a.size(), 0);

    auto snap = l.snapshot();
    REQUIRE(snap->length() == 4);
    REQUIRE(snap->readWrapped(0) == 0.1f);
    REQUIRE(snap->readWrapped(3) == 0.4f);

    l.clearAudio();
    // Snapshot must remain unchanged.
    REQUIRE(snap->readWrapped(0) == 0.1f);
    REQUIRE(snap->readWrapped(3) == 0.4f);
}
