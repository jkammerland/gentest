#include "gentest/runner.h"
#include "gentest/runner_fmt.h"
#include "public/gentest_textual_suite_mocks.hpp"
#include "support/context_proof_support.h"

using namespace gentest::asserts;

#include <atomic>
#include <chrono>
#include <future>
#include <latch>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <vector>

namespace [[using gentest: suite("concurrency")]] gentest_concurrency_tests {

[[using gentest: test("child_log_pass")]]
void child_log_pass() {
    gentest::set_log_policy(gentest::LogPolicy::Always);
    auto              context = gentest::get_current_context();
    std::promise<int> result;
    auto              done = result.get_future();
    std::thread       t([context, result = std::move(result)]() mutable {
        auto guard = gentest::set_current_context(context);
        gentest::log("child worker logged");
        result.set_value(2);
    });
    t.join();
    ASSERT_EQ(done.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    EXPECT_EQ(done.get(), 2);
}

[[using gentest: test("child_expect_fail")]]
void child_expect_fail() {
    auto        context = gentest::get_current_context();
    std::thread t([context] {
        // Intentionally forget to set the current context to exercise the fatal no-current-context path.
        (void)context;
        EXPECT_TRUE(false, "child thread EXPECT_TRUE(false)");
        EXPECT_EQ(1, 2, "child thread EXPECT_EQ(1,2)");
    });
    t.join();
}

[[using gentest: test("child_skip_no_context")]]
void child_skip_no_context() {
    std::thread t([] { gentest::skip("child thread skip without context"); });
    t.join();
}

[[using gentest: test("child_xfail_no_context")]]
void child_xfail_no_context() {
    std::thread t([] { gentest::xfail("child thread xfail without context"); });
    t.join();
}

[[using gentest: test("child_reports_exception_pass")]]
void child_reports_exception_pass() {
    auto               context = gentest::get_current_context();
    std::promise<bool> result;
    auto               done = result.get_future();
    std::thread        t([context, result = std::move(result)]() mutable {
        auto guard = gentest::set_current_context(context);
        try {
            throw std::runtime_error("boom");
        } catch (const std::runtime_error &) { result.set_value(true); } catch (...) {
            result.set_value(false);
        }
    });
    t.join();
    ASSERT_EQ(done.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    EXPECT_TRUE(done.get());
}

[[using gentest: test("multi_adopt_log_pass")]]
void multi_adopt_log_pass() {
    gentest::set_log_policy(gentest::LogPolicy::Always);
    auto             context = gentest::get_current_context();
    std::atomic<int> completed{0};
    std::thread      t1([context, &completed] {
        auto g = gentest::set_current_context(context);
        gentest::log("adopted worker 1");
        completed.fetch_add(1, std::memory_order_acq_rel);
    });
    std::thread      t2([context, &completed] {
        auto g = gentest::set_current_context(context);
        gentest::log("adopted worker 2");
        completed.fetch_add(1, std::memory_order_acq_rel);
    });
    std::thread      t3([context, &completed] {
        auto g = gentest::set_current_context(context);
        gentest::log("adopted worker 3");
        completed.fetch_add(1, std::memory_order_acq_rel);
    });
    t1.join();
    t2.join();
    t3.join();
    EXPECT_EQ(completed.load(std::memory_order_acquire), 3);
}

[[using gentest: test("mock_adopt_dispatch")]]
void mock_adopt_dispatch() {
    constexpr int kThreads         = 8;
    constexpr int kCallsPerThread  = 128;
    constexpr int kExpectedInvokes = kThreads * kCallsPerThread;

    gentest::mock<mocking::Ticker> mock_tick;
    std::atomic<int>               sum{0};
    EXPECT_CALL(mock_tick, tick).times(kExpectedInvokes).invokes([&](int value) { sum.fetch_add(value, std::memory_order_relaxed); });

    auto                     context = gentest::get_current_context();
    std::latch               ready(kThreads);
    std::latch               start(1);
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&mock_tick, &ready, &start, context] {
            auto guard = gentest::set_current_context(context);
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

[[using gentest: test("adopted_expect_pass_death")]]
void adopted_expect_pass_death() {
    auto        context = gentest::get_current_context();
    std::thread t([context] {
        auto g = gentest::set_current_context(context);
        EXPECT_TRUE(true);
    });
    t.join();
}

[[using gentest: test("adopted_fmt_expect_pass_death")]]
void adopted_fmt_expect_pass_death() {
    auto        context = gentest::get_current_context();
    std::thread t([context] {
        auto g = gentest::set_current_context(context);
        EXPECT_TRUE(true, "formatted {}", 1);
    });
    t.join();
}

[[using gentest: test("adopted_expect_fail_death")]]
void adopted_expect_fail_death() {
    auto        context = gentest::get_current_context();
    std::thread t([context] {
        auto g = gentest::set_current_context(context);
        EXPECT_TRUE(false, "adopted EXPECT_TRUE(false)");
    });
    t.join();
}

[[using gentest: test("adopted_assert_death")]]
void adopted_assert_death() {
    auto        context = gentest::get_current_context();
    std::thread t([context] {
        auto g = gentest::set_current_context(context);
        ASSERT_TRUE(false, "adopted ASSERT_TRUE(false)");
    });
    t.join();
}

[[using gentest: test("adopted_expect_throw_death")]]
void adopted_expect_throw_death() {
    auto        context = gentest::get_current_context();
    std::thread t([context] {
        auto g = gentest::set_current_context(context);
        EXPECT_THROW(throw std::runtime_error("boom"), std::runtime_error);
    });
    t.join();
}

[[using gentest: test("adopted_fail_death")]]
void adopted_fail_death() {
    auto        context = gentest::get_current_context();
    std::thread t([context] {
        auto g = gentest::set_current_context(context);
        gentest::fail("adopted fail");
    });
    t.join();
}

[[using gentest: test("adopted_skip_death")]]
void adopted_skip_death() {
    auto        context = gentest::get_current_context();
    std::thread t([context] {
        auto g = gentest::set_current_context(context);
        gentest::skip("adopted skip");
    });
    t.join();
}

[[using gentest: test("adopted_xfail_death")]]
void adopted_xfail_death() {
    auto        context = gentest::get_current_context();
    std::thread t([context] {
        auto g = gentest::set_current_context(context);
        gentest::xfail("adopted xfail");
    });
    t.join();
}

[[using gentest: test("adopted_log_policy_death")]]
void adopted_log_policy_death() {
    auto        context = gentest::get_current_context();
    std::thread t([context] {
        auto g = gentest::set_current_context(context);
        gentest::set_log_policy(gentest::LogPolicy::Always);
    });
    t.join();
}

[[using gentest: test("adopted_default_log_policy_death")]]
void adopted_default_log_policy_death() {
    auto        context = gentest::get_current_context();
    std::thread t([context] {
        auto g = gentest::set_current_context(context);
        gentest::set_default_log_policy(gentest::LogPolicy::Always);
    });
    t.join();
}

[[using gentest: test("adopted_mock_expectation_death")]]
void adopted_mock_expectation_death() {
    gentest::mock<mocking::Ticker> mock_tick;
    auto                           context = gentest::get_current_context();
    std::thread                    t([context, &mock_tick] {
        auto g = gentest::set_current_context(context);
        EXPECT_CALL(mock_tick, tick).times(1);
    });
    t.join();
}

[[using gentest: test("adopted_mock_mode_death")]]
void adopted_mock_mode_death() {
    gentest::mock<mocking::Ticker> mock_tick;
    auto                           context = gentest::get_current_context();
    std::thread                    t([context, &mock_tick] {
        auto g = gentest::set_current_context(context);
        gentest::make_nice(mock_tick);
    });
    t.join();
}

[[using gentest: test("adopted_mock_handle_mutation_death")]]
void adopted_mock_handle_mutation_death() {
    gentest::mock<mocking::Ticker> mock_tick;
    auto                           handle  = EXPECT_CALL(mock_tick, tick);
    auto                           context = gentest::get_current_context();
    std::thread                    t([context, handle = std::move(handle)]() mutable {
        auto g = gentest::set_current_context(context);
        handle.times(1);
    });
    t.join();
}

[[using gentest: test("adopted_mock_closed_context_death")]]
void adopted_mock_closed_context_death() {
    gentest::mock<mocking::Ticker> mock_tick;
    gentest::CurrentContext        context;
    {
        gentest::test_support::ActiveProofContext proof_context("closed mock proof");
        context = gentest::get_current_context();
    }

    auto g = gentest::set_current_context(context);
    gentest::make_nice(mock_tick);
}

[[using gentest: test("adopted_mock_unexpected_call_death")]]
void adopted_mock_unexpected_call_death() {
    gentest::mock<mocking::Ticker> mock_tick;
    auto                           context = gentest::get_current_context();
    std::thread                    t([context, &mock_tick] {
        auto g = gentest::set_current_context(context);
        mock_tick.tick(1);
    });
    t.join();
}

[[using gentest: test("stop_callback_expect_death")]]
void stop_callback_expect_death() {
    auto ctx = gentest::detail::current_test();
    ASSERT_TRUE(static_cast<bool>(ctx));

    std::stop_callback callback(ctx->stop_source.get_token(), [] { EXPECT_TRUE(true); });
    gentest::detail::request_context_stop(*ctx);
}

[[using gentest: test("no_adopt_expect_death_multi")]]
void no_adopt_expect_death_multi() {
    std::thread t1([] { EXPECT_TRUE(false, "no adopt t1"); });
    std::thread t2([] { EXPECT_EQ(1, 2, "no adopt t2"); });
    t1.join();
    t2.join();
}

} // namespace gentest_concurrency_tests
