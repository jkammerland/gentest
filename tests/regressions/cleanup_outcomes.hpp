#pragma once

#include "gentest/runner.h"

#include <chrono>
#include <cstdio>

namespace cleanup_outcomes {

struct Blocked : gentest::AsyncFixtureTearDown {
    gentest::awaitable<> tearDown() override {
        gentest::async::promise<void> promise;
        auto                          done = promise.get_future();
        promise.set_blocked("close dependency unavailable");
        co_await done.wait("fixture close");
    }
};

[[gentest::test("blocked")]] gentest::awaitable<> blocked(Blocked &);

struct Assertion : gentest::FixtureTearDown {
    void tearDown() override {
        // A consumer can throw the public exception without recording a diagnostic.
        // NOLINTNEXTLINE(bugprone-std-exception-baseclass)
        throw gentest::assertion("cleanup invariant failed");
    }
};

[[gentest::test("assertion")]] void assertion(Assertion &);

namespace cancel {

inline gentest::async::manual_event entered;
inline gentest::async::manual_event never_released;

struct Early : gentest::FixtureTearDown {
    void tearDown() override { (void)std::fputs("hook: released\n", stderr); }
};

struct Yielding : gentest::AsyncFixtureTearDown {
    gentest::awaitable<> tearDown() override {
        (void)std::fputs("hook: yielding\n", stderr);
        for (;;)
            co_await gentest::async::yield();
    }
};

struct Stalled : gentest::AsyncFixtureTearDown {
    gentest::awaitable<> tearDown() override {
        (void)std::fputs("hook: timer started\n", stderr);
        co_await gentest::async::sleep_for(std::chrono::hours(24));
        (void)std::fputs("hook: timer finished\n", stderr);
    }
};

struct Last : gentest::AsyncFixtureTearDown {
    gentest::awaitable<> tearDown() override {
        (void)std::fputs("hook: last started\n", stderr);
        entered.set("teardown entered");
        co_await never_released.wait("unavailable dependency");
        (void)std::fputs("hook: last finished\n", stderr);
    }
};

[[gentest::test("01_pending")]] gentest::awaitable<> pending(Early &, Yielding &, Stalled &, Last &);

[[gentest::test("02_peer")]] gentest::awaitable<> peer();

} // namespace cancel
} // namespace cleanup_outcomes
