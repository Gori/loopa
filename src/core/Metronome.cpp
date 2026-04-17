#include "Metronome.h"

#include <cmath>

namespace loopa {

namespace {
constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kClickSeconds = 0.03;      // ~30 ms click
constexpr double kBeatFreq    = 1800.0;     // plain beat click
constexpr double kDownbeatFreq = 2800.0;    // bar-0 accent
}  // namespace

void Metronome::prepareToPlay(double sampleRate) noexcept {
    m_sampleRate = sampleRate;
    m_clickLen = static_cast<int>(kClickSeconds * sampleRate + 0.5);
    m_remaining = 0;
    m_phase = 0;
    m_offsetStart = 0;
    m_freq = kBeatFreq;
}

void Metronome::trigger(int offsetSamples, bool downbeat) noexcept {
    if (m_sampleRate <= 0.0 || m_clickLen <= 0) {
        return;
    }
    m_remaining = m_clickLen;
    m_phase = 0;
    m_offsetStart = offsetSamples < 0 ? 0 : offsetSamples;
    m_freq = downbeat ? kDownbeatFreq : kBeatFreq;
}

void Metronome::render(float* out, int numSamples) noexcept {
    if (m_remaining <= 0 || out == nullptr) {
        return;
    }
    const double tauSamples = static_cast<double>(m_clickLen) * 0.25;  // fast exp decay
    int i = m_offsetStart;
    m_offsetStart = 0;  // only skip once per block
    while (i < numSamples && m_remaining > 0) {
        const double env = std::exp(-static_cast<double>(m_phase) / tauSamples);
        const double s = std::sin(kTwoPi * m_freq * static_cast<double>(m_phase) / m_sampleRate);
        out[i] += static_cast<float>(env * s) * m_amplitude;
        ++i;
        ++m_phase;
        --m_remaining;
    }
}

}  // namespace loopa
