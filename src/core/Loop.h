#pragma once

#include "core/AudioBuffer.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace loopa {

// A playable loop: sample data (mono float, trimmed to an exact sample count
// matching its (bars, BPM) pair) + metadata. Loops are reference-counted so
// undo/redo can hold earlier snapshots without copying audio.
class Loop {
public:
    Loop() = default;

    Loop(int bars, double bpm, double sampleRate, std::size_t lengthSamples);

    int bars() const noexcept           { return m_bars; }
    double bpm() const noexcept         { return m_bpm; }
    double sampleRate() const noexcept  { return m_sampleRate; }
    std::size_t length() const noexcept { return m_buffer.size(); }
    bool empty() const noexcept         { return m_buffer.size() == 0; }

    // Monotonically increases every time the loop's sample data changes.
    // Readers (UI / render path) use it to invalidate caches derived from the
    // samples (e.g. GPU peak buffers).
    std::uint64_t version() const noexcept { return m_version.load(std::memory_order_relaxed); }

    const float* data() const noexcept  { return m_buffer.data(); }
    float* data() noexcept              { return m_buffer.data(); }

    // Read the sample at an arbitrary (possibly out-of-range) position.
    // Wraps modulo length() so playback is seamless.
    float readWrapped(std::size_t pos) const noexcept {
        return m_buffer.data()[pos % m_buffer.size()];
    }

    // Overdub: destructively sum `src[0..n)` into `m_buffer[offset..offset+n)`,
    // with wrap. Clamps to [-1, 1] to prevent runaway levels.
    void overdubSum(const float* src, std::size_t n, std::size_t offset) noexcept;

    // Replace: overwrite `m_buffer[offset..offset+n)` with `src`, with wrap.
    void replaceRegion(const float* src, std::size_t n, std::size_t offset) noexcept;

    // Zero every sample.
    void clearAudio() noexcept;

    // Create an immutable snapshot (deep-copy of samples) for undo/redo.
    std::shared_ptr<Loop> snapshot() const;

    Loop(const Loop&) = delete;
    Loop& operator=(const Loop&) = delete;
    Loop(Loop&&) = delete;
    Loop& operator=(Loop&&) = delete;

private:
    int m_bars = 0;
    double m_bpm = 0.0;
    double m_sampleRate = 0.0;
    AudioBuffer m_buffer;
    std::atomic<std::uint64_t> m_version{1};
};

}  // namespace loopa
