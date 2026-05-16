#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/AiPreprocess/AiPreprocess.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using loopa::AiPreprocessor;

namespace {

std::string emptyModelDir() {
    auto dir = std::filesystem::temp_directory_path() / "loopa-aipreprocess-tests-empty";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}

std::vector<float> makeSine(double freq, double sr, std::size_t n, float amp) {
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = amp * static_cast<float>(
            std::sin(2.0 * M_PI * freq * static_cast<double>(i) / sr));
    }
    return out;
}

}  // namespace

TEST_CASE("AiPreprocessor pass-through when all options off — bit-exact",
          "[aipreprocess]") {
    AiPreprocessor pre(emptyModelDir());
    auto in = makeSine(440.0, 48000.0, 48000, 0.5f);
    auto buf = in;

    AiPreprocessor::Options opts{};
    REQUIRE(pre.process(buf.data(), buf.size(), 48000.0, opts));
    REQUIRE(std::memcmp(buf.data(), in.data(), in.size() * sizeof(float)) == 0);
}

TEST_CASE("AiPreprocessor null/empty inputs are no-ops", "[aipreprocess]") {
    AiPreprocessor pre(emptyModelDir());
    AiPreprocessor::Options opts{};
    opts.loudnessNormalize = true;

    REQUIRE(pre.process(nullptr, 0, 48000.0, opts));

    std::vector<float> buf(0);
    REQUIRE(pre.process(buf.data(), 0, 48000.0, opts));
}

TEST_CASE("AiPreprocessor missing denoise/vocal models skip gracefully",
          "[aipreprocess]") {
    AiPreprocessor pre(emptyModelDir());
    auto in = makeSine(440.0, 48000.0, 48000, 0.5f);
    auto buf = in;

    AiPreprocessor::Options opts{};
    opts.denoise      = true;
    opts.vocalIsolate = true;
    REQUIRE(pre.process(buf.data(), buf.size(), 48000.0, opts));
    REQUIRE(std::memcmp(buf.data(), in.data(), in.size() * sizeof(float)) == 0);
}

TEST_CASE("AiPreprocessor LUFS normalize is self-consistent",
          "[aipreprocess][lufs]") {
    AiPreprocessor pre(emptyModelDir());
    // 1 kHz sinusoid, amplitude 0.1, 3 seconds — long enough for the 400 ms
    // blocks + relative gating to integrate cleanly. K-weighting at 1 kHz
    // adds roughly +0.7 dB so absolute LUFS depends on the K-weighting
    // implementation; instead of asserting an exact post-normalize peak,
    // we run the normalizer twice with the same target and verify the
    // second pass leaves the buffer essentially unchanged (gain ≈ 0 dB).
    auto buf = makeSine(1000.0, 48000.0, 48000 * 3, 0.1f);

    AiPreprocessor::Options opts{};
    opts.loudnessNormalize = true;
    opts.targetLufs        = -18.0f;
    REQUIRE(pre.process(buf.data(), buf.size(), 48000.0, opts));

    float peakAfterFirst = 0.0f;
    for (float v : buf) peakAfterFirst = std::max(peakAfterFirst, std::fabs(v));
    REQUIRE(peakAfterFirst > 0.1f);   // sanity: gain was applied
    REQUIRE(peakAfterFirst < 0.99f);  // sanity: no clipping at this target

    auto snapshot = buf;
    REQUIRE(pre.process(buf.data(), buf.size(), 48000.0, opts));

    // Second pass should be ~unity gain → peaks within 0.1 dB (≈ 1.2%).
    float peakAfterSecond = 0.0f;
    for (float v : buf) peakAfterSecond = std::max(peakAfterSecond, std::fabs(v));
    REQUIRE(peakAfterSecond == Catch::Approx(peakAfterFirst).epsilon(0.012));
}

TEST_CASE("AiPreprocessor LUFS normalize on silence is a no-op",
          "[aipreprocess][lufs]") {
    AiPreprocessor pre(emptyModelDir());
    std::vector<float> buf(48000 * 2, 0.0f);

    AiPreprocessor::Options opts{};
    opts.loudnessNormalize = true;
    REQUIRE(pre.process(buf.data(), buf.size(), 48000.0, opts));

    for (float v : buf) REQUIRE(v == 0.0f);
}
