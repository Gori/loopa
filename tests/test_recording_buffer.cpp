#include <catch2/catch_test_macros.hpp>

#include "core/RecordingBuffer.h"

#include <vector>

using loopa::RecordingBuffer;

TEST_CASE("RecordingBuffer starts empty and has the requested capacity", "[recbuf]") {
    RecordingBuffer rb(1024);
    REQUIRE(rb.capacity() == 1024);
    REQUIRE(rb.written() == 0);
    REQUIRE_FALSE(rb.overflowed());
}

TEST_CASE("RecordingBuffer append advances written and preserves data", "[recbuf]") {
    RecordingBuffer rb(16);
    const std::vector<float> a = {0.1f, 0.2f, 0.3f, 0.4f};
    REQUIRE(rb.append(a.data(), a.size()) == 4);
    REQUIRE(rb.written() == 4);

    const std::vector<float> b = {0.5f, 0.6f};
    REQUIRE(rb.append(b.data(), b.size()) == 2);
    REQUIRE(rb.written() == 6);

    REQUIRE(rb.data()[0] == 0.1f);
    REQUIRE(rb.data()[3] == 0.4f);
    REQUIRE(rb.data()[4] == 0.5f);
    REQUIRE(rb.data()[5] == 0.6f);
    REQUIRE_FALSE(rb.overflowed());
}

TEST_CASE("RecordingBuffer sets overflowed flag and returns short count on full write", "[recbuf]") {
    RecordingBuffer rb(4);
    const std::vector<float> a = {1.0f, 1.0f, 1.0f};
    REQUIRE(rb.append(a.data(), a.size()) == 3);
    REQUIRE_FALSE(rb.overflowed());

    const std::vector<float> b = {2.0f, 2.0f};  // only 1 sample fits
    REQUIRE(rb.append(b.data(), b.size()) == 1);
    REQUIRE(rb.written() == 4);
    REQUIRE(rb.overflowed());
    REQUIRE(rb.data()[3] == 2.0f);
}

TEST_CASE("RecordingBuffer reset clears state but keeps capacity", "[recbuf]") {
    RecordingBuffer rb(8);
    const std::vector<float> a(10, 1.0f);
    rb.append(a.data(), a.size());
    REQUIRE(rb.overflowed());
    REQUIRE(rb.written() == 8);

    rb.reset();
    REQUIRE(rb.written() == 0);
    REQUIRE_FALSE(rb.overflowed());
    REQUIRE(rb.capacity() == 8);

    const std::vector<float> b = {5.0f, 6.0f};
    REQUIRE(rb.append(b.data(), b.size()) == 2);
    REQUIRE(rb.data()[0] == 5.0f);
}

TEST_CASE("RecordingBuffer append handles null src and zero n without touching state", "[recbuf]") {
    RecordingBuffer rb(8);
    REQUIRE(rb.append(nullptr, 10) == 0);
    REQUIRE(rb.written() == 0);

    const float sample = 1.0f;
    REQUIRE(rb.append(&sample, 0) == 0);
    REQUIRE(rb.written() == 0);
}
