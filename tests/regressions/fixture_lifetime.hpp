#pragma once

#include "gentest/runner.h"
#include "public/gentest_textual_suite_mocks.hpp"

#include <functional>
#include <latch>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <thread>

namespace fixture_lifetime {

inline int releases = 0;

struct First : gentest::FixtureTearDown {
    void tearDown() override {
        ++releases;
        gentest::record_property("release_count", releases);
        gentest::expect_eq(releases, 2, "first fixture must be released last");
    }
};

struct Second : gentest::FixtureTearDown {
    void tearDown() override {
        ++releases;
        gentest::fail("second fixture failed to close");
    }
};

struct AsyncSecond : gentest::AsyncFixtureTearDown {
    gentest::awaitable<> tearDown() override {
        co_await gentest::async::yield();
        ++releases;
        throw std::runtime_error("async fixture failed to close");
    }
};

[[gentest::test("sync/01_skip")]] inline void      sync_skip(First &, Second &) { gentest::skip("sync prerequisite unavailable"); }
[[gentest::test("sync/02_following")]] inline void following() { gentest::expect_eq(releases, 2); }

[[gentest::test("async/01_skip")]] inline gentest::awaitable<> async_skip(First &, AsyncSecond &) {
    co_await gentest::async::yield();
    gentest::skip("async prerequisite unavailable");
}

struct SkipSetup : Second, gentest::FixtureSetup {
    void setUp() override { gentest::skip("setup prerequisite unavailable"); }
};

struct Unarmed : gentest::FixtureTearDown {
    void tearDown() override { gentest::fail("unstarted fixture was torn down"); }
};

[[gentest::test("setup_skip")]] inline void setup_skip(First &, SkipSetup &, Unarmed &) { gentest::fail("body ran after setup skipped"); }

struct SkipCleanup : gentest::FixtureTearDown {
    void tearDown() override { gentest::skip("cleanup prerequisite unavailable"); }
};

[[gentest::test("body_and_teardown_skip")]] inline void body_and_teardown_skip(First &, Second &, SkipCleanup &) {
    gentest::skip("original body skip");
}

[[gentest::test("teardown_skip")]] inline void teardown_skip(First &, Second &, SkipCleanup &) {}

[[gentest::test("body_throws")]] inline void body_throws(First &, Second &) { throw std::runtime_error("body exception before cleanup"); }

struct MeasuredFirst : First {
    ~MeasuredFirst() override { gentest::record_property("destroyed_during_teardown", true); }
};

[[using gentest: bench("measured_cleanup_bench"), bench_once]] inline void measured_cleanup_bench(MeasuredFirst &, Second &) {
    gentest::skip("measurement prerequisite unavailable");
}

[[gentest::jitter("measured_cleanup_jitter")]] inline void measured_cleanup_jitter(MeasuredFirst &, Second &) {
    gentest::skip("measurement prerequisite unavailable");
}

struct [[gentest::fixture(suite)]] MockOwner {
    gentest::mock<mocking::Calculator> calculator;
};

[[gentest::test("shared_missing")]] inline void shared_missing(MockOwner &owner) {
    gentest::expect(owner.calculator, &mocking::Calculator::reset).times(1);
}

struct [[gentest::fixture(suite)]] Worker : gentest::FixtureTearDown {
    gentest::CurrentContext                                  callback_context;
    bool                                                     stale_admitted = false;
    std::latch                                               ready{1};
    std::latch                                               stop{1};
    std::thread                                              worker;
    std::optional<std::stop_callback<std::function<void()>>> callback;

    void start(bool log_after_join) {
        auto context = gentest::get_current_context();
        worker       = std::thread([this, context] {
            auto lease = gentest::set_current_context(context);
            ready.count_down();
            stop.wait();
        });
        ready.wait();

        // The owner must still be able to report after the last lease is released.
        callback.emplace(context.stop_token(), [this, context, log_after_join] {
            stop.count_down();
            worker.join();
            if (log_after_join) {
                gentest::log("stop callback joined worker");

                // Capture the callback's current context after the previous worker drained.
                callback_context = gentest::get_current_context();
                std::thread reporter([this, context] {
                    stale_admitted = can_adopt(context);
                    auto lease     = gentest::set_current_context(callback_context);
                    {
                        auto nested = gentest::set_current_context(gentest::get_current_context());
                        gentest::log("nested reporter lease");
                    }
                    gentest::log("reporter restored outer lease");
                });
                reporter.join();

                auto nested = gentest::set_current_context(context);
                gentest::log("nested callback lease");
            }
        });
    }

    static bool can_adopt(const gentest::CurrentContext &context) {
        auto lease = gentest::set_current_context(context);
        return static_cast<bool>(gentest::get_current_context());
    }

    void tearDown() override {
        // The handoff handle can survive case completion in this shared fixture.
        if (callback_context) {
            gentest::expect(!stale_admitted, "pre-stop handle reopened adoption after the last worker joined");
            gentest::expect(!can_adopt(callback_context), "callback handle reopened the finished case");
        }
    }
};

[[gentest::test("worker_stop")]] inline void worker_stop(Worker &worker) { worker.start(true); }

[[gentest::test("worker_exception")]] inline gentest::awaitable<> worker_exception(Worker &worker) {
    worker.start(false);
    co_await gentest::async::yield();
    throw std::runtime_error("owner failed after starting worker");
}

} // namespace fixture_lifetime
