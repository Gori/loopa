#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace loopa {

// Single-producer / single-consumer lock-free ring. Fixed capacity known at compile time.
// Producer thread: tryPush(). Consumer thread: tryPop(). Never blocks, never allocates.
// Capacity must be a power of two for branch-free wrap; enforced at compile time.
template <typename T, std::size_t Capacity>
class LockFreeQueue {
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(std::is_nothrow_move_constructible_v<T> || std::is_trivially_copyable_v<T>,
                  "T must be nothrow-movable or trivially copyable");

public:
    LockFreeQueue() = default;
    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;

    static constexpr std::size_t capacity() noexcept { return Capacity; }

    bool tryPush(T value) noexcept {
        const std::size_t head = m_head.load(std::memory_order_relaxed);
        const std::size_t tail = m_tail.load(std::memory_order_acquire);
        if (head - tail >= Capacity) {
            return false;  // full
        }
        m_storage[head & (Capacity - 1)] = std::move(value);
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> tryPop() noexcept {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        const std::size_t head = m_head.load(std::memory_order_acquire);
        if (tail == head) {
            return std::nullopt;  // empty
        }
        T out = std::move(m_storage[tail & (Capacity - 1)]);
        m_tail.store(tail + 1, std::memory_order_release);
        return out;
    }

    // Approximate size. Producer/consumer only call this on their own side.
    std::size_t sizeApprox() const noexcept {
        const std::size_t head = m_head.load(std::memory_order_acquire);
        const std::size_t tail = m_tail.load(std::memory_order_acquire);
        return head - tail;
    }

    bool emptyApprox() const noexcept { return sizeApprox() == 0; }
    bool fullApprox() const noexcept { return sizeApprox() >= Capacity; }

private:
    alignas(64) std::atomic<std::size_t> m_head{0};
    alignas(64) std::atomic<std::size_t> m_tail{0};
    alignas(64) std::array<T, Capacity> m_storage{};
};

}  // namespace loopa
