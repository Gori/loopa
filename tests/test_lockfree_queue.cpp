#include <catch2/catch_test_macros.hpp>

#include "core/LockFreeQueue.h"

#include <atomic>
#include <cstdint>
#include <thread>

using loopa::LockFreeQueue;

TEST_CASE("LockFreeQueue basic push/pop and empty/full", "[lockfree]") {
    LockFreeQueue<int, 4> q;
    REQUIRE(q.emptyApprox());
    REQUIRE_FALSE(q.tryPop().has_value());

    REQUIRE(q.tryPush(1));
    REQUIRE(q.tryPush(2));
    REQUIRE(q.tryPush(3));
    REQUIRE(q.tryPush(4));
    REQUIRE(q.fullApprox());
    REQUIRE_FALSE(q.tryPush(5));  // rejected when full

    REQUIRE(q.tryPop().value() == 1);
    REQUIRE(q.tryPop().value() == 2);
    REQUIRE(q.tryPop().value() == 3);
    REQUIRE(q.tryPop().value() == 4);
    REQUIRE(q.emptyApprox());
}

TEST_CASE("LockFreeQueue wraps correctly across capacity", "[lockfree]") {
    LockFreeQueue<int, 4> q;
    for (int cycle = 0; cycle < 100; ++cycle) {
        REQUIRE(q.tryPush(cycle * 10 + 1));
        REQUIRE(q.tryPush(cycle * 10 + 2));
        REQUIRE(q.tryPush(cycle * 10 + 3));
        REQUIRE(q.tryPop().value() == cycle * 10 + 1);
        REQUIRE(q.tryPop().value() == cycle * 10 + 2);
        REQUIRE(q.tryPush(cycle * 10 + 4));
        REQUIRE(q.tryPop().value() == cycle * 10 + 3);
        REQUIRE(q.tryPop().value() == cycle * 10 + 4);
    }
    REQUIRE(q.emptyApprox());
}

TEST_CASE("LockFreeQueue SPSC with two threads moves every item exactly once", "[lockfree]") {
    constexpr int kN = 100'000;
    LockFreeQueue<int, 1024> q;

    std::atomic<std::int64_t> sumConsumed{0};

    std::thread consumer([&] {
        int remaining = kN;
        while (remaining > 0) {
            if (auto v = q.tryPop()) {
                sumConsumed.fetch_add(*v, std::memory_order_relaxed);
                --remaining;
            }
        }
    });

    for (int i = 0; i < kN; ++i) {
        while (!q.tryPush(i)) {
            // busy-wait, producer is ahead of consumer
        }
    }

    consumer.join();

    const std::int64_t expected = static_cast<std::int64_t>(kN) * (kN - 1) / 2;
    REQUIRE(sumConsumed.load() == expected);
}
