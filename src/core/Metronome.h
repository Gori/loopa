#pragma once

#include <cstddef>

namespace loopa {

// Tiny click synth. `trigger()` starts a click (short decaying sine burst).
// `render()` mixes any in-progress click into the output buffer. A downbeat
// click is pitched higher than a plain beat click.
class Metronome {
public:
    void prepareToPlay(double sampleRate) noexcept;

    void trigger(int offsetSamples, bool downbeat) noexcept;

    // Mix the click sound into the output (mono — caller replicates to channels).
    void render(float* out, int numSamples) noexcept;

    bool isActive() const noexcept { return m_remaining > 0; }

    void setAmplitude(float a) noexcept { m_amplitude = a < 0.0f ? 0.0f : a; }
    float amplitude() const noexcept    { return m_amplitude; }

private:
    double m_sampleRate = 0.0;
    int    m_clickLen = 0;      // total samples in a click envelope
    int    m_remaining = 0;     // samples left to render in current click
    int    m_offsetStart = 0;   // offset within the next processBlock where the click starts
    int    m_phase = 0;         // sample index within current click
    double m_freq = 0.0;
    float  m_amplitude = 0.35f;
};

}  // namespace loopa
