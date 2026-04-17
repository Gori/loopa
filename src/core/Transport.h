#pragma once

#include <cstddef>

namespace loopa {

// Global transport: tracks play state and a sample-accurate position that
// wraps at the longest loop's length. The UI polls for bar/beat display; the
// audio thread advances it every block.
class Transport {
public:
    struct Timebase {
        double bpm = 0.0;
        double sampleRate = 0.0;
        int beatsPerBar = 4;
    };

    Transport() = default;

    void setTimebase(Timebase tb) noexcept           { m_timebase = tb; }
    Timebase timebase() const noexcept               { return m_timebase; }

    // The total length in samples, matching the longest currently-loaded loop.
    // Position wraps at this value. Zero means "no loops yet" — position stays
    // at zero and bar queries return zero.
    void setTotalLengthSamples(std::size_t n) noexcept {
        m_totalLengthSamples = n;
        if (n > 0 && m_position >= n) {
            m_position %= n;
        }
    }
    std::size_t totalLengthSamples() const noexcept  { return m_totalLengthSamples; }

    // Authoritative bars-per-cycle, set by the engine alongside totalLengthSamples.
    // When set, currentBar() and totalBars() derive samplesPerBar from
    // totalLengthSamples / totalBars instead of from BPM rounding, keeping
    // display consistent with the actually-looped sample count.
    void setTotalBars(int bars) noexcept { m_totalBars = bars; }

    void setPlaying(bool playing) noexcept           { m_playing = playing; }
    bool isPlaying() const noexcept                  { return m_playing; }

    void resetPosition() noexcept                    { m_position = 0; }
    std::size_t samplePosition() const noexcept      { return m_position; }

    // Advance the position by `n` samples, wrapping at totalLengthSamples.
    // Called from the audio thread every block when playing. No-op if not
    // playing or total length is zero.
    void advance(std::size_t n) noexcept {
        if (!m_playing || m_totalLengthSamples == 0) {
            return;
        }
        m_position = (m_position + n) % m_totalLengthSamples;
    }

    std::size_t samplesPerBeat() const noexcept {
        if (m_timebase.bpm <= 0.0 || m_timebase.sampleRate <= 0.0) {
            return 0;
        }
        const double sec = 60.0 / m_timebase.bpm;
        return static_cast<std::size_t>(sec * m_timebase.sampleRate + 0.5);
    }

    std::size_t samplesPerBar() const noexcept {
        if (m_totalBars > 0 && m_totalLengthSamples > 0) {
            return m_totalLengthSamples / static_cast<std::size_t>(m_totalBars);
        }
        return samplesPerBeat() * static_cast<std::size_t>(m_timebase.beatsPerBar);
    }

    // Current bar within the total loop window, 0-indexed. Zero if no timebase
    // or no total length.
    int currentBar() const noexcept {
        const auto spb = samplesPerBar();
        if (spb == 0) {
            return 0;
        }
        return static_cast<int>(m_position / spb);
    }

    // Current beat within the current bar, 0-indexed.
    int currentBeat() const noexcept {
        const auto spb_beat = samplesPerBeat();
        const auto spb_bar  = samplesPerBar();
        if (spb_beat == 0 || spb_bar == 0) {
            return 0;
        }
        const auto samplesInBar = m_position % spb_bar;
        return static_cast<int>(samplesInBar / spb_beat);
    }

    // Total number of bars that fit in the loop window. When setTotalBars has
    // been called, returns that value directly (authoritative); otherwise falls
    // back to length/samplesPerBar.
    int totalBars() const noexcept {
        if (m_totalBars > 0 && m_totalLengthSamples > 0) {
            return m_totalBars;
        }
        const auto spb = samplesPerBar();
        if (spb == 0 || m_totalLengthSamples == 0) {
            return 0;
        }
        return static_cast<int>(m_totalLengthSamples / spb);
    }

private:
    Timebase m_timebase{};
    std::size_t m_totalLengthSamples = 0;
    std::size_t m_position = 0;
    int m_totalBars = 0;
    bool m_playing = false;
};

}  // namespace loopa
