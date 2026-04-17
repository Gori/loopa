#include "Loop.h"

#include <cstring>

namespace loopa {

namespace {

float clampToUnit(float x) noexcept {
    if (x > 1.0f)  return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x;
}

}  // namespace

Loop::Loop(int bars, double bpm, double sampleRate, std::size_t lengthSamples)
    : m_bars(bars),
      m_bpm(bpm),
      m_sampleRate(sampleRate),
      m_buffer(lengthSamples) {
    m_buffer.setSize(lengthSamples);
}

void Loop::overdubSum(const float* src, std::size_t n, std::size_t offset) noexcept {
    const std::size_t len = m_buffer.size();
    if (len == 0 || src == nullptr || n == 0) {
        return;
    }
    float* dst = m_buffer.data();
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t idx = (offset + i) % len;
        dst[idx] = clampToUnit(dst[idx] + src[i]);
    }
    m_version.fetch_add(1, std::memory_order_relaxed);
}

void Loop::replaceRegion(const float* src, std::size_t n, std::size_t offset) noexcept {
    const std::size_t len = m_buffer.size();
    if (len == 0 || src == nullptr || n == 0) {
        return;
    }
    float* dst = m_buffer.data();
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t idx = (offset + i) % len;
        dst[idx] = src[i];
    }
    m_version.fetch_add(1, std::memory_order_relaxed);
}

void Loop::clearAudio() noexcept {
    m_buffer.zero();
    m_version.fetch_add(1, std::memory_order_relaxed);
}

std::shared_ptr<Loop> Loop::snapshot() const {
    auto out = std::make_shared<Loop>(m_bars, m_bpm, m_sampleRate, m_buffer.size());
    const std::size_t n = m_buffer.size();
    if (n > 0) {
        std::memcpy(out->m_buffer.data(), m_buffer.data(), n * sizeof(float));
    }
    return out;
}

}  // namespace loopa
