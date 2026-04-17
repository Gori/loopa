#pragma once

#include <array>
#include <cstddef>
#include <optional>

namespace loopa {

struct BpmFit {
    int bars;      // 1, 2, 4, 8, or 16
    double bpm;    // rounded to 2 decimals, in [60.00, 140.00]
};

struct BpmDetectorConfig {
    double minBpm = 60.0;
    double maxBpm = 140.0;
    int beatsPerBar = 4;
    std::array<int, 5> allowedBars{{1, 2, 4, 8, 16}};
};

// Pure function: given a recording length in samples and a sample rate, decide
// which (bars, BPM) pair the user intended. Returns nullopt if no fit falls
// within [minBpm, maxBpm] — the recording is too short or too long to map to
// any supported loop length. The rule: among valid fits, pick the one with the
// LARGEST bars count (long-bar fits preferred for long recordings).
std::optional<BpmFit> detectBpm(std::size_t sampleCount,
                                double sampleRate,
                                const BpmDetectorConfig& cfg = {}) noexcept;

// Once a (bars, BPM) fit is chosen, compute the exact integer sample count that
// corresponds to `bars` bars at `bpm`. Loops are trimmed to this length for
// sample-accurate looping.
std::size_t samplesForBars(int bars, double bpm, double sampleRate, int beatsPerBar = 4) noexcept;

}  // namespace loopa
