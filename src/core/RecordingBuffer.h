#pragma once

#include "core/AudioBuffer.h"

#include <cstddef>

namespace loopa {

// Pre-allocated, bounded record destination for a single track. Capacity is
// fixed at construction (sized for the longest possible recording — e.g. 16
// bars @ 60 BPM @ 48 kHz). Writes beyond capacity are treated as a hard error
// (m_overflowed flag set, no silent drop) — the caller must cap their writes.
class RecordingBuffer {
public:
    explicit RecordingBuffer(std::size_t capacitySamples)
        : m_buffer(capacitySamples) {}

    std::size_t capacity() const noexcept  { return m_buffer.capacity(); }
    std::size_t written() const noexcept   { return m_written; }
    bool overflowed() const noexcept       { return m_overflowed; }

    const float* data() const noexcept     { return m_buffer.data(); }
    float* data() noexcept                 { return m_buffer.data(); }

    void reset() noexcept {
        m_written = 0;
        m_overflowed = false;
    }

    // Append n samples from src. Returns how many samples were actually written.
    // If the write would exceed capacity, writes what fits, sets overflowed=true,
    // and returns the number of samples written before the overflow.
    std::size_t append(const float* src, std::size_t n) noexcept {
        if (src == nullptr || n == 0) {
            return 0;
        }
        const std::size_t cap = m_buffer.capacity();
        const std::size_t room = (m_written >= cap) ? 0 : (cap - m_written);
        const std::size_t toCopy = (n <= room) ? n : room;
        float* dst = m_buffer.data();
        for (std::size_t i = 0; i < toCopy; ++i) {
            dst[m_written + i] = src[i];
        }
        m_written += toCopy;
        if (toCopy < n) {
            m_overflowed = true;
        }
        return toCopy;
    }

    RecordingBuffer(const RecordingBuffer&) = delete;
    RecordingBuffer& operator=(const RecordingBuffer&) = delete;
    RecordingBuffer(RecordingBuffer&&) noexcept = default;
    RecordingBuffer& operator=(RecordingBuffer&&) noexcept = default;

private:
    AudioBuffer m_buffer;
    std::size_t m_written = 0;
    bool m_overflowed = false;
};

}  // namespace loopa
