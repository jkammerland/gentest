#include "gentest/runner.h"
#include "public/gentest_textual_suite_mocks.hpp"

using namespace gentest::asserts;

#include <atomic>
#include <latch>
#include <stdexcept>
#include <thread>
#include <vector>

namespace concurrency {

[[using gentest: test("child_expect_pass")]]
void child_expect_pass() {
    auto        tok = gentest::ctx::current();
    std::thread t([tok] {
        gentest::ctx::Adopt guard(tok);
        EXPECT_TRUE(true);
        EXPECT_EQ(1, 1);
    });
    t.join();
}

[[using gentest: test("child_expect_fail")]]
void child_expect_fail() {
    auto        tok = gentest::ctx::current();
    std::thread t([tok] {
        // Intentionally forget to adopt to exercise global fallback
        (void)tok;
        EXPECT_TRUE(false, "child thread EXPECT_TRUE(false)");
        EXPECT_EQ(1, 2, "child thread EXPECT_EQ(1,2)");
    });
    t.join();
}

[[using gentest: test("child_expect_throw_pass")]]
void child_expect_throw_pass() {
    auto        tok = gentest::ctx::current();
    std::thread t([tok] {
        gentest::ctx::Adopt guard(tok);
        EXPECT_THROW(throw std::runtime_error("boom"), std::runtime_error);
        EXPECT_THROW(throw 123, int);
    });
    t.join();
}

} // namespace concurrency

namespace concurrency {

[[using gentest: test("multi_adopt_expect_pass")]]
void multi_adopt_expect_pass() {
    auto        tok = gentest::ctx::current();
    std::thread t1([tok] {
        gentest::ctx::Adopt g(tok);
        EXPECT_TRUE(true);
    });
    std::thread t2([tok] {
        gentest::ctx::Adopt g(tok);
        EXPECT_EQ(10, 10);
    });
    std::thread t3([tok] {
        gentest::ctx::Adopt g(tok);
        EXPECT_NE(1, 2);
    });
    t1.join();
    t2.join();
    t3.join();
}

[[using gentest: test("mock_adopt_dispatch")]]
void mock_adopt_dispatch() {
    constexpr int kThreads         = 8;
    constexpr int kCallsPerThread  = 128;
    constexpr int kExpectedInvokes = kThreads * kCallsPerThread;

    gentest::mock<mocking::Ticker> mock_tick;
    std::atomic<int>               sum{0};
    EXPECT_CALL(mock_tick, tick).times(kExpectedInvokes).invokes([&](int value) { sum.fetch_add(value, std::memory_order_relaxed); });

    auto                     tok = gentest::ctx::current();
    std::latch               ready(kThreads);
    std::latch               start(1);
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&mock_tick, &ready, &start, tok] {
            gentest::ctx::Adopt guard(tok);
            ready.count_down();
            start.wait();
            for (int call = 0; call < kCallsPerThread; ++call) {
                mock_tick.tick(1);
            }
        });
    }

    ready.wait();
    start.count_down();
    for (auto &thread : threads) {
        thread.join();
    }

    EXPECT_EQ(sum.load(std::memory_order_relaxed), kExpectedInvokes);
}

[[using gentest: test("multi_adopt_expect_fail")]]
void multi_adopt_expect_fail() {
    auto        tok = gentest::ctx::current();
    std::thread t1([tok] {
        gentest::ctx::Adopt g(tok);
        EXPECT_TRUE(false, "multi t1");
    });
    std::thread t2([tok] {
        gentest::ctx::Adopt g(tok);
        EXPECT_EQ(1, 2, "multi t2");
    });
    std::thread t3([tok] {
        gentest::ctx::Adopt g(tok);
        EXPECT_NE(3, 3, "multi t3");
    });
    t1.join();
    t2.join();
    t3.join();
}

[[using gentest: test("no_adopt_expect_death_multi")]]
void no_adopt_expect_death_multi() {
    std::thread t1([] { EXPECT_TRUE(false, "no adopt t1"); });
    std::thread t2([] { EXPECT_EQ(1, 2, "no adopt t2"); });
    t1.join();
    t2.join();
}

} // namespace concurrency
