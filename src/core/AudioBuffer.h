#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>

namespace loopa {

// Fixed-capacity mono float buffer. Capacity is set at construction time
// and never changes. `m_size` is the logical length (<= capacity). No
// allocation after construction, so the audio thread can freely write
// up to `capacity()` samples.
class AudioBuffer {
public:
    AudioBuffer() = default;

    explicit AudioBuffer(std::size_t capacity)
        : m_data(capacity > 0 ? std::make_unique<float[]>(capacity) : nullptr),
          m_capacity(capacity),
          m_size(0) {
        for (std::size_t i = 0; i < capacity; ++i) {
            m_data[i] = 0.0f;
        }
    }

    AudioBuffer(const AudioBuffer&) = delete;
    AudioBuffer& operator=(const AudioBuffer&) = delete;

    AudioBuffer(AudioBuffer&& other) noexcept
        : m_data(std::move(other.m_data)),
          m_capacity(other.m_capacity),
          m_size(other.m_size) {
        other.m_capacity = 0;
        other.m_size = 0;
    }

    AudioBuffer& operator=(AudioBuffer&& other) noexcept {
        if (this != &other) {
            m_data = std::move(other.m_data);
            m_capacity = other.m_capacity;
            m_size = other.m_size;
            other.m_capacity = 0;
            other.m_size = 0;
        }
        return *this;
    }

    std::size_t capacity() const noexcept { return m_capacity; }
    std::size_t size() const noexcept { return m_size; }

    void setSize(std::size_t n) noexcept {
        assert(n <= m_capacity);
        m_size = n;
    }

    void clear() noexcept { m_size = 0; }

    void zero() noexcept {
        for (std::size_t i = 0; i < m_capacity; ++i) {
            m_data[i] = 0.0f;
        }
    }

    float* data() noexcept { return m_data.get(); }
    const float* data() const noexcept { return m_data.get(); }

    float& operator[](std::size_t i) noexcept {
        assert(i < m_capacity);
        return m_data[i];
    }
    float operator[](std::size_t i) const noexcept {
        assert(i < m_capacity);
        return m_data[i];
    }

private:
    std::unique_ptr<float[]> m_data;
    std::size_t m_capacity = 0;
    std::size_t m_size = 0;
};

}  // namespace loopa
