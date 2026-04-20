#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/Loop.h"
#include "core/Timbre/TimbreTransfer.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

#ifndef LOOPA_MODELS_DIR_STR
#define LOOPA_MODELS_DIR_STR ""
#endif

namespace {

constexpr double kSR = 48000.0;
// At 24kHz (model rate), one chunk = 131072 samples. One chunk's worth at
// 48kHz engine rate = 262144 samples ~= 5.46 s. Tests below pick source
// durations to force 1, 2, and 3 chunks so the chunking path is exercised
// in full.
constexpr std::size_t kOneChunkSamples   = static_cast<std::size_t>(5.0 * kSR);   // 5.0 s -> 1 chunk
constexpr std::size_t kTwoChunkSamples   = static_cast<std::size_t>(7.0 * kSR);   // 7.0 s -> 2 chunks
constexpr std::size_t kThreeChunkSamples = static_cast<std::size_t>(12.0 * kSR);  // 12.0 s -> 3 chunks

// Length in the 48kHz output Loop corresponding to one model chunk.
// Used to slice output into per-chunk RMS windows.
constexpr std::size_t kChunkOutputAt48k = 262144;

std::shared_ptr<loopa::Loop> makeNoiseLoop(std::size_t lengthSamples, float amp,
                                             unsigned seed = 42) {
    auto loop = std::make_shared<loopa::Loop>(4, 120.0, kSR, lengthSamples);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-amp, amp);
    float* data = loop->data();
    for (std::size_t i = 0; i < lengthSamples; ++i) data[i] = dist(rng);
    return loop;
}

double chunkRms(const loopa::Loop& l, std::size_t start, std::size_t end) {
    end = std::min(end, l.length());
    if (end <= start) return 0.0;
    double sumSq = 0.0;
    const float* d = l.data();
    for (std::size_t i = start; i < end; ++i) sumSq += double(d[i]) * d[i];
    return std::sqrt(sumSq / double(end - start));
}

std::vector<loopa::TimbrePreset> discoverPresets(const std::string& dir) {
    std::vector<loopa::TimbrePreset> out;
    std::error_code ec;
    if (dir.empty() || !std::filesystem::exists(dir, ec)) return out;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        const auto p = entry.path();
        const auto name = p.filename().string();
        if (name.rfind("timbre_", 0) == 0 && p.extension() == ".pt") {
            out.push_back({name, p.string()});
        }
    }
    return out;
}

bool haveModels() {
    const std::string dir = LOOPA_MODELS_DIR_STR;
    if (dir.empty()) return false;
    for (const char* f : {"ae.ts", "enc_t.ts", "enc_s.ts", "time_xform.ts", "denoise.ts"}) {
        if (!std::filesystem::exists(dir + "/" + f)) return false;
    }
    return true;
}

}  // namespace

// ------------------------------------------------------------------
// Basic smoke: the pipeline loads, runs, and produces finite output.
// ------------------------------------------------------------------
TEST_CASE("TimbreTransferEngine runs the ISMIR pipeline end-to-end", "[timbre]") {
    if (!haveModels()) SKIP("ISMIR model files missing from LOOPA_MODELS_DIR");
    const std::string dir = LOOPA_MODELS_DIR_STR;
    auto presets = discoverPresets(dir);
    loopa::TimbreTransferEngine engine(dir, presets);

    auto src = makeNoiseLoop(kOneChunkSamples, 0.3f);
    auto out = engine.transfer(src, 0);

    REQUIRE(out != nullptr);
    REQUIRE(out->length() == kOneChunkSamples);
    REQUIRE(out->bars() == src->bars());
    REQUIRE(out->bpm() == Catch::Approx(src->bpm()));
    REQUIRE(out->sampleRate() == Catch::Approx(src->sampleRate()));

    double sumSq = 0.0;
    float peak = 0.0f;
    const float* d = out->data();
    for (std::size_t i = 0; i < out->length(); ++i) {
        REQUIRE(std::isfinite(d[i]));
        peak = std::max(peak, std::fabs(d[i]));
        sumSq += double(d[i]) * d[i];
    }
    REQUIRE(peak > 0.0f);
    REQUIRE(peak <= 1.0f);                              // hard-limited
    REQUIRE(std::sqrt(sumSq / out->length()) > 0.01);   // not silent
}

// ------------------------------------------------------------------
// Chunking proof suite. For 2-chunk and 3-chunk source durations, the
// pipeline must:
//   (a) produce no silent chunk (every model chunk returns audible audio);
//   (b) produce comparable energy across chunks (no chunk is <10% of the
//       median RMS — that would indicate a chunk dropout).
// Run under LOOPA_TIMBRE_DUMP so per-chunk RMS is visible in the log.
// ------------------------------------------------------------------
TEST_CASE("chunking: 2-chunk source — every chunk produces audible audio",
          "[timbre][chunking]") {
    if (!haveModels()) SKIP("ISMIR model files missing from LOOPA_MODELS_DIR");
    const std::string dir = LOOPA_MODELS_DIR_STR;
    loopa::TimbreTransferEngine engine(dir, discoverPresets(dir));

    auto src = makeNoiseLoop(kTwoChunkSamples, 0.3f, /*seed=*/11);
    auto out = engine.transfer(src, 0);
    REQUIRE(out != nullptr);
    REQUIRE(out->length() == kTwoChunkSamples);

    // Evaluate each output chunk's RMS. With 7s source, chunk 2 doesn't
    // fully cover the source; we clamp the window end to source length.
    const double rmsA = chunkRms(*out, 0,                      kChunkOutputAt48k);
    const double rmsB = chunkRms(*out, kChunkOutputAt48k,      kTwoChunkSamples);

    INFO("chunkA=" << rmsA << " chunkB=" << rmsB);
    REQUIRE(rmsA > 0.01);      // audible
    REQUIRE(rmsB > 0.01);      // audible — this is the one that regressed before
    const double ratio = std::min(rmsA, rmsB) / std::max(rmsA, rmsB);
    REQUIRE(ratio > 0.1);      // no chunk is <10% of the other's energy
}

TEST_CASE("chunking: 3-chunk source — every chunk produces audible audio",
          "[timbre][chunking]") {
    if (!haveModels()) SKIP("ISMIR model files missing from LOOPA_MODELS_DIR");
    const std::string dir = LOOPA_MODELS_DIR_STR;
    loopa::TimbreTransferEngine engine(dir, discoverPresets(dir));

    auto src = makeNoiseLoop(kThreeChunkSamples, 0.3f, /*seed=*/22);
    auto out = engine.transfer(src, 0);
    REQUIRE(out != nullptr);
    REQUIRE(out->length() == kThreeChunkSamples);

    const double rmsA = chunkRms(*out, 0,                        kChunkOutputAt48k);
    const double rmsB = chunkRms(*out, kChunkOutputAt48k,        2 * kChunkOutputAt48k);
    const double rmsC = chunkRms(*out, 2 * kChunkOutputAt48k,    kThreeChunkSamples);

    INFO("chunkA=" << rmsA << " chunkB=" << rmsB << " chunkC=" << rmsC);
    REQUIRE(rmsA > 0.01);
    REQUIRE(rmsB > 0.01);
    REQUIRE(rmsC > 0.01);

    // All three chunks should be roughly comparable in energy — no chunk
    // is <10% of the median.
    std::array<double, 3> v{rmsA, rmsB, rmsC};
    std::sort(v.begin(), v.end());
    const double median = v[1];
    for (double r : v) REQUIRE(r > 0.1 * median);
}

TEST_CASE("chunking: length preservation through the pipeline",
          "[timbre][chunking]") {
    if (!haveModels()) SKIP("ISMIR model files missing from LOOPA_MODELS_DIR");
    const std::string dir = LOOPA_MODELS_DIR_STR;
    loopa::TimbreTransferEngine engine(dir, discoverPresets(dir));

    for (std::size_t n : {kOneChunkSamples, kTwoChunkSamples, kThreeChunkSamples}) {
        auto src = makeNoiseLoop(n, 0.3f);
        auto out = engine.transfer(src, 0);
        REQUIRE(out != nullptr);
        REQUIRE(out->length() == n);
        for (std::size_t i = 0; i < n; ++i) REQUIRE(std::isfinite(out->data()[i]));
    }
}

// ------------------------------------------------------------------
// Preset exposure: menu labels come straight from the engine.
// ------------------------------------------------------------------
TEST_CASE("TimbreTransferEngine exposes preset names", "[timbre]") {
    if (!haveModels()) SKIP("ISMIR model files missing from LOOPA_MODELS_DIR");
    const std::string dir = LOOPA_MODELS_DIR_STR;
    auto presets = discoverPresets(dir);
    loopa::TimbreTransferEngine engine(dir, presets);
    REQUIRE(engine.numTimbres() == static_cast<int>(presets.size()));
    if (engine.numTimbres() > 0) REQUIRE(!engine.timbreName(0).empty());
}
