#include "BpmDetector.h"

#include <cmath>

namespace loopa {

namespace {

double roundTo2Decimals(double x) noexcept {
    return std::round(x * 100.0) / 100.0;
}

}  // namespace

std::optional<BpmFit> detectBpm(std::size_t sampleCount,
                                double sampleRate,
                                const BpmDetectorConfig& cfg) noexcept {
    if (sampleCount == 0 || sampleRate <= 0.0) {
        return std::nullopt;
    }

    std::optional<BpmFit> best;
    int bestBars = -1;

    const double samples = static_cast<double>(sampleCount);
    const double beatsPerBar = static_cast<double>(cfg.beatsPerBar);

    for (const int bars : cfg.allowedBars) {
        // bpm = (bars * beatsPerBar) beats / (samples / sampleRate seconds) * 60 s/min
        const double totalBeats = static_cast<double>(bars) * beatsPerBar;
        const double bpm = totalBeats * 60.0 * sampleRate / samples;
        const double rounded = roundTo2Decimals(bpm);
        if (rounded < cfg.minBpm || rounded > cfg.maxBpm) {
            continue;
        }
        if (bars > bestBars) {
            best = BpmFit{bars, rounded};
            bestBars = bars;
        }
    }

    return best;
}

std::size_t samplesForBars(int bars, double bpm, double sampleRate, int beatsPerBar) noexcept {
    const double beats = static_cast<double>(bars) * static_cast<double>(beatsPerBar);
    const double seconds = beats * 60.0 / bpm;
    const double samples = seconds * sampleRate;
    return static_cast<std::size_t>(std::llround(samples));
}

}  // namespace loopa
