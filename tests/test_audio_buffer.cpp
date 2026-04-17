#include <catch2/catch_test_macros.hpp>

#include "core/AudioBuffer.h"

using loopa::AudioBuffer;

TEST_CASE("AudioBuffer allocates once with the given capacity", "[audiobuffer]") {
    AudioBuffer b(1024);
    REQUIRE(b.capacity() == 1024);
    REQUIRE(b.size() == 0);
    REQUIRE(b.data() != nullptr);
    for (std::size_t i = 0; i < 1024; ++i) {
        REQUIRE(b[i] == 0.0f);
    }
}

TEST_CASE("AudioBuffer setSize changes logical length but not capacity", "[audiobuffer]") {
    AudioBuffer b(256);
    b.setSize(200);
    REQUIRE(b.size() == 200);
    REQUIRE(b.capacity() == 256);

    b.setSize(0);
    REQUIRE(b.size() == 0);

    b.setSize(256);
    REQUIRE(b.size() == 256);
}

TEST_CASE("AudioBuffer stores and reads back sample values", "[audiobuffer]") {
    AudioBuffer b(16);
    b.setSize(16);
    for (std::size_t i = 0; i < 16; ++i) {
        b[i] = static_cast<float>(i) * 0.1f;
    }
    for (std::size_t i = 0; i < 16; ++i) {
        REQUIRE(b[i] == static_cast<float>(i) * 0.1f);
    }
}

TEST_CASE("AudioBuffer zero() clears all samples", "[audiobuffer]") {
    AudioBuffer b(8);
    b.setSize(8);
    for (std::size_t i = 0; i < 8; ++i) {
        b[i] = 1.0f;
    }
    b.zero();
    for (std::size_t i = 0; i < 8; ++i) {
        REQUIRE(b[i] == 0.0f);
    }
}

TEST_CASE("AudioBuffer is movable", "[audiobuffer]") {
    AudioBuffer a(64);
    a.setSize(32);
    a[10] = 0.5f;

    AudioBuffer b = std::move(a);
    REQUIRE(b.capacity() == 64);
    REQUIRE(b.size() == 32);
    REQUIRE(b[10] == 0.5f);
}
