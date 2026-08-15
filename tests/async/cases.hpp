#pragma once

#include "gentest/attributes.h"
#include "gentest/detail/runtime_context.h"
#include "gentest/runner.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace gentest::asserts;

namespace async {

inline gentest::async::manual_event server_ready;
inline gentest::async::manual_event client_done;
inline std::vector<std::string>     order;

inline gentest::async::manual_event concurrent_lvalue_value_child_started;
inline gentest::async::manual_event concurrent_lvalue_value_child_release;
inline gentest::async::manual_event concurrent_lvalue_void_child_started;
inline gentest::async::manual_event concurrent_lvalue_void_child_release;
inline gentest::async::manual_event concurrent_lvalue_first_waiter_done;
inline gentest::async_test<int>     concurrent_lvalue_value_task;
inline gentest::async_test<void>    concurrent_lvalue_void_task;

inline void reset_batch_if_previous_round_completed() {
    if (server_ready.is_set("batch server ready") && client_done.is_set("batch client done")) {
        server_ready.reset_all();
        client_done.reset_all();
        order.clear();
    }
}

inline gentest::async::manual_event live_demo_resume;
inline std::vector<std::string>     live_demo_order;

inline gentest::async::manual_event fail_fast_snapshot_release;
inline gentest::async::manual_event fail_fast_cancel_adopted_context_resume;
inline gentest::async::manual_event fail_fast_cancel_local_fixture_resume;
inline gentest::async::manual_event fail_fast_cancel_adopted_local_fixture_resume;
inline gentest::async::manual_event fail_fast_cancel_slow_adopted_resume;
inline gentest::async::manual_event fail_fast_cancel_adopted_skip_resume;
inline gentest::async::manual_event fail_fast_final_drain_fail_release;
inline gentest::async::manual_event fail_fast_final_drain_late_release;
inline gentest::async::manual_event local_unresumable_teardown_never;
inline std::atomic<int>             fail_fast_final_drain_waiters{0};
inline std::atomic<bool>            fail_fast_self_adopted_worker_started{false};
inline std::atomic<bool>            fail_fast_self_adopted_worker_done{true};
inline std::atomic<bool>            fail_fast_cancel_adopted_context_worker_started{false};
inline std::atomic<bool>            fail_fast_cancel_adopted_context_worker_done{true};
inline std::atomic<bool>            fail_fast_cancel_released_context_worker_started{false};
inline std::atomic<bool>            fail_fast_cancel_released_context_worker_done{true};
inline std::atomic<bool>            fail_fast_cancel_adopted_local_fixture_started{false};
inline std::atomic<bool>            fail_fast_cancel_adopted_local_fixture_worker_done{true};
inline std::atomic<bool>            fail_fast_cancel_adopted_local_fixture_torn_down{true};
inline std::atomic<bool>            fail_fast_cancel_adopted_local_fixture_frame_destroyed{true};
inline std::atomic<bool>            fail_fast_cancel_slow_adopted_worker_started{false};
inline std::atomic<bool>            fail_fast_cancel_slow_adopted_worker_done{true};
inline std::atomic<bool>            fail_fast_cancel_slow_adopted_frame_destroyed{true};
inline std::atomic<bool>            fail_fast_cancel_adopted_skip_worker_started{false};
inline std::atomic<bool>            fail_fast_cancel_adopted_skip_worker_done{true};

inline gentest::async::manual_event                          group_fence_release;
inline std::vector<std::string>                              group_fence_order;
inline std::vector<std::string>                              timer_fairness_order;
inline gentest::async::manual_event                          timer_stress_start;
inline gentest::async::manual_event                          timer_stress_all_started;
inline gentest::async::manual_event                          timer_stress_all_done;
inline std::atomic<int>                                      timer_stress_started{0};
inline std::atomic<int>                                      timer_stress_completed{0};
inline std::atomic<std::chrono::steady_clock::duration::rep> timer_stress_deadline_ticks{0};
constexpr int                                                kTimerStressSleepers   = 4;
constexpr int                                                kTimerStressIterations = 32;

class JoiningThread {
  public:
    explicit JoiningThread(std::thread thread) : thread_(std::move(thread)) {}

    JoiningThread(const JoiningThread &)            = delete;
    JoiningThread &operator=(const JoiningThread &) = delete;

    JoiningThread(JoiningThread &&) noexcept            = delete;
    JoiningThread &operator=(JoiningThread &&) noexcept = delete;

    ~JoiningThread() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

  private:
    std::thread thread_;
};

inline gentest::async::manual_event suite_group_fence_release;
inline std::vector<std::string>     suite_group_fence_order;

inline gentest::async::manual_event process_exit_event;

struct ProcessExitSignal {
    ~ProcessExitSignal() { process_exit_event.set("exit"); }
};

inline ProcessExitSignal process_exit_signal;

struct FailFastSelfAdoptedExitWait {
    ~FailFastSelfAdoptedExitWait() {
        if (!fail_fast_self_adopted_worker_started.load(std::memory_order_acquire)) {
            return;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!fail_fast_self_adopted_worker_done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!fail_fast_self_adopted_worker_done.load(std::memory_order_acquire)) {
            (void)std::fputs("fail-fast self-adopted worker did not finish\n", stderr);
            std::abort();
        }
    }
};

inline FailFastSelfAdoptedExitWait fail_fast_self_adopted_exit_wait;

struct FailFastCancelAdoptedContextExitWait {
    ~FailFastCancelAdoptedContextExitWait() {
        if (!fail_fast_cancel_adopted_context_worker_started.load(std::memory_order_acquire)) {
            return;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!fail_fast_cancel_adopted_context_worker_done.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!fail_fast_cancel_adopted_context_worker_done.load(std::memory_order_acquire)) {
            (void)std::fputs("fail-fast adopted context worker did not finish\n", stderr);
            std::abort();
        }
    }
};

inline FailFastCancelAdoptedContextExitWait fail_fast_cancel_adopted_context_exit_wait;

struct FailFastCancelReleasedContextExitWait {
    ~FailFastCancelReleasedContextExitWait() {
        if (!fail_fast_cancel_released_context_worker_started.load(std::memory_order_acquire)) {
            return;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!fail_fast_cancel_released_context_worker_done.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!fail_fast_cancel_released_context_worker_done.load(std::memory_order_acquire)) {
            (void)std::fputs("fail-fast released adopted context worker did not finish\n", stderr);
            std::abort();
        }
    }
};

inline FailFastCancelReleasedContextExitWait fail_fast_cancel_released_context_exit_wait;

struct FailFastCancelAdoptedLocalFixtureExitWait {
    ~FailFastCancelAdoptedLocalFixtureExitWait() {
        if (!fail_fast_cancel_adopted_local_fixture_started.load(std::memory_order_acquire)) {
            return;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!fail_fast_cancel_adopted_local_fixture_worker_done.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!fail_fast_cancel_adopted_local_fixture_worker_done.load(std::memory_order_acquire)) {
            (void)std::fputs("fail-fast adopted local fixture worker did not finish\n", stderr);
            std::abort();
        }
        if (!fail_fast_cancel_adopted_local_fixture_torn_down.load(std::memory_order_acquire)) {
            (void)std::fputs("fail-fast canceled adopted async local fixture without teardown\n", stderr);
            std::abort();
        }
        if (!fail_fast_cancel_adopted_local_fixture_frame_destroyed.load(std::memory_order_acquire)) {
            (void)std::fputs("fail-fast canceled adopted async frame was not destroyed after adopted release\n", stderr);
            std::abort();
        }
    }
};

inline FailFastCancelAdoptedLocalFixtureExitWait fail_fast_cancel_adopted_local_fixture_exit_wait;

struct FailFastCancelSlowAdoptedExitWait {
    ~FailFastCancelSlowAdoptedExitWait() {
        if (!fail_fast_cancel_slow_adopted_worker_started.load(std::memory_order_acquire)) {
            return;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!fail_fast_cancel_slow_adopted_worker_done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!fail_fast_cancel_slow_adopted_worker_done.load(std::memory_order_acquire)) {
            (void)std::fputs("fail-fast slow adopted worker did not finish\n", stderr);
            std::abort();
        }
        if (!fail_fast_cancel_slow_adopted_frame_destroyed.load(std::memory_order_acquire)) {
            (void)std::fputs("fail-fast slow canceled adopted async frame was not destroyed after adopted release\n", stderr);
            std::abort();
        }
    }
};

inline FailFastCancelSlowAdoptedExitWait fail_fast_cancel_slow_adopted_exit_wait;

struct FailFastCancelAdoptedSkipExitWait {
    ~FailFastCancelAdoptedSkipExitWait() {
        if (!fail_fast_cancel_adopted_skip_worker_started.load(std::memory_order_acquire)) {
            return;
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!fail_fast_cancel_adopted_skip_worker_done.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!fail_fast_cancel_adopted_skip_worker_done.load(std::memory_order_acquire)) {
            (void)std::fputs("fail-fast adopted skip worker did not finish\n", stderr);
            std::abort();
        }
    }
};

inline FailFastCancelAdoptedSkipExitWait fail_fast_cancel_adopted_skip_exit_wait;

inline gentest::async::manual_event                   adopted_resume_event;
inline std::shared_ptr<gentest::async::promise<void>> promise_order_source;
inline std::shared_ptr<gentest::async::promise<void>> explicit_blocked_source;
inline std::shared_ptr<gentest::async::promise<void>> empty_blocked_source;

constexpr int kLiveDemoRounds = 10;

struct LocalAsyncFixture : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    int value = 0;

    gentest::async_test<void> setUp() override {
        co_await gentest::async::yield();
        value = 42;
    }

    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        value = 0;
    }
};

struct LocalSyncFixture : gentest::FixtureSetup {
    int value = 0;

    void setUp() override { value = 7; }
};

struct LocalAsyncLifecycleFixture : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    int  value     = 0;
    bool setup     = false;
    bool torn_down = false;

    ~LocalAsyncLifecycleFixture() override {
        if (setup && !torn_down) {
            std::abort();
        }
    }

    gentest::async_test<void> setUp() override {
        co_await gentest::async::yield();
        setup = true;
        value = 123;
    }

    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        torn_down = true;
        value     = 0;
    }
};

struct FailFastCancelLocalFixture : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    bool setup     = false;
    bool torn_down = false;

    ~FailFastCancelLocalFixture() override {
        if (setup && !torn_down) {
            (void)std::fputs("fail-fast canceled async local fixture without teardown\n", stderr);
            (void)std::fflush(stderr);
            std::abort();
        }
    }

    gentest::async_test<void> setUp() override {
        co_await gentest::async::yield();
        setup = true;
    }

    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        torn_down = true;
    }
};

struct FailFastCancelAdoptedLocalFixture : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    bool setup     = false;
    bool torn_down = false;

    ~FailFastCancelAdoptedLocalFixture() override {
        if (setup && !torn_down) {
            (void)std::fputs("fail-fast canceled adopted async local fixture destroyed without teardown\n", stderr);
            (void)std::fflush(stderr);
            std::abort();
        }
    }

    gentest::async_test<void> setUp() override {
        co_await gentest::async::yield();
        fail_fast_cancel_adopted_local_fixture_torn_down.store(false, std::memory_order_release);
        setup = true;
    }

    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        torn_down = true;
        fail_fast_cancel_adopted_local_fixture_torn_down.store(true, std::memory_order_release);
    }
};

struct FailFastCancelAdoptedLocalFrameGuard {
    ~FailFastCancelAdoptedLocalFrameGuard() {
        fail_fast_cancel_adopted_local_fixture_frame_destroyed.store(true, std::memory_order_release);
    }
};

struct FailFastCancelSlowAdoptedFrameGuard {
    ~FailFastCancelSlowAdoptedFrameGuard() { fail_fast_cancel_slow_adopted_frame_destroyed.store(true, std::memory_order_release); }
};

struct UnsupportedSuspend {
    [[nodiscard]] constexpr bool await_ready() const noexcept { return false; }
    constexpr void               await_suspend(std::coroutine_handle<>) const noexcept {}
    constexpr void               await_resume() const noexcept {}
};

gentest::async_test<void> wait_for_child_pass();

gentest::async_test<int> wait_for_child_value();

gentest::async_test<void> wait_for_child_throws();

gentest::async_test<void> wait_for_child_never(gentest::async::manual_event &event);

gentest::async_test<void> wait_for_nested_child_never(gentest::async::manual_event &event);

gentest::async_test<int> concurrent_lvalue_value_child();

gentest::async_test<void> concurrent_lvalue_void_child();

struct [[using gentest: fixture(suite)]] SharedSuiteAsyncFixture : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    static inline int                      setups    = 0;
    static inline SharedSuiteAsyncFixture *first     = nullptr;
    int                                    value     = 0;
    bool                                   saw_async = false;
    bool                                   saw_sync  = false;
    bool                                   saw_check = false;
    bool                                   torn_down = false;
    gentest::async::manual_event           release_async;
    std::vector<std::string>               events;

    ~SharedSuiteAsyncFixture() override {
        if ((saw_async || saw_sync || saw_check) && !torn_down) {
            std::abort();
        }
    }

    gentest::async_test<void> setUp() override {
        co_await gentest::async::yield();
        ++setups;
        first     = nullptr;
        value     = 10;
        saw_async = false;
        saw_sync  = false;
        saw_check = false;
        torn_down = false;
        release_async.reset_all();
        events.clear();
    }

    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        if (saw_async || saw_sync || saw_check) {
            EXPECT_TRUE(saw_async);
            EXPECT_TRUE(saw_sync);
            EXPECT_TRUE(saw_check);
            EXPECT_EQ(value, 20);
        }
        torn_down = true;
    }
};

struct [[using gentest: fixture(global)]] SharedGlobalAsyncFixture : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    static inline int                       setups    = 0;
    static inline SharedGlobalAsyncFixture *first     = nullptr;
    int                                     value     = 0;
    bool                                    saw_seed  = false;
    bool                                    saw_async = false;
    bool                                    saw_sync  = false;
    bool                                    saw_check = false;
    bool                                    torn_down = false;
    gentest::async::manual_event            release_async;
    std::vector<std::string>                events;

    ~SharedGlobalAsyncFixture() override {
        if ((saw_seed || saw_async || saw_sync || saw_check) && !torn_down) {
            std::abort();
        }
    }

    gentest::async_test<void> setUp() override {
        co_await gentest::async::yield();
        ++setups;
        first     = nullptr;
        value     = 100;
        saw_seed  = false;
        saw_async = false;
        saw_sync  = false;
        saw_check = false;
        torn_down = false;
        release_async.reset_all();
        events.clear();
    }

    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        if (saw_seed || saw_async || saw_sync || saw_check) {
            EXPECT_TRUE(saw_seed);
            EXPECT_TRUE(saw_async);
            EXPECT_TRUE(saw_sync);
            EXPECT_TRUE(saw_check);
            EXPECT_EQ(value, 200);
        }
        torn_down = true;
    }
};

struct [[using gentest: fixture(suite)]] BlockingSchedulerAdoptedFixture : gentest::AsyncFixtureSetup {
    bool setup = false;

    gentest::async_test<void> setUp() override {
        gentest::async::manual_event release;
        auto                         context = gentest::get_current_context();
        auto                         started = std::make_shared<std::promise<void>>();
        auto                         ready   = started->get_future();
        std::promise<void>           resumed;
        auto                         resumed_ready = resumed.get_future();
        std::atomic<bool>            worker_saw_resume{false};

        {
            JoiningThread worker(std::thread([context = std::move(context), started = std::move(started), &release,
                                              resumed_ready = std::move(resumed_ready), &worker_saw_resume]() mutable {
                auto lease = gentest::set_current_context(context);
                started->set_value();
                release.set("adopted fixture setup released");
                worker_saw_resume.store(resumed_ready.wait_for(std::chrono::seconds(1)) == std::future_status::ready,
                                        std::memory_order_release);
            }));

            ready.wait();
            co_await release.wait("adopted fixture setup released");
            resumed.set_value();
        }
        EXPECT_TRUE(worker_saw_resume.load(std::memory_order_acquire));
        setup = true;
    }
};

using SharedGlobalAsyncHandle   = std::shared_ptr<SharedGlobalAsyncFixture>;
using SharedGlobalAsyncRawAlias = SharedGlobalAsyncFixture *;

struct [[using gentest: fixture(suite)]] SharedSuiteUnresumableFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    int                          value     = 0;
    bool                         saw_async = false;
    bool                         saw_sync  = false;
    bool                         torn_down = false;
    gentest::async::manual_event never;

    ~SharedSuiteUnresumableFixture() override {
        if ((saw_async || saw_sync) && !torn_down) {
            std::abort();
        }
    }

    void setUp() override {
        value     = 300;
        saw_async = false;
        saw_sync  = false;
        torn_down = false;
        never.reset_all();
    }

    void tearDown() override {
        if (saw_async || saw_sync) {
            EXPECT_TRUE(saw_async);
            EXPECT_TRUE(saw_sync);
            EXPECT_EQ(value, 301);
        }
        torn_down = true;
    }
};

struct [[using gentest: fixture(global)]] SharedGlobalUnresumableFixture : gentest::FixtureSetup, gentest::FixtureTearDown {
    int                          value     = 0;
    bool                         saw_async = false;
    bool                         saw_sync  = false;
    bool                         torn_down = false;
    gentest::async::manual_event never;

    ~SharedGlobalUnresumableFixture() override {
        if ((saw_async || saw_sync) && !torn_down) {
            std::abort();
        }
    }

    void setUp() override {
        value     = 400;
        saw_async = false;
        saw_sync  = false;
        torn_down = false;
        never.reset_all();
    }

    void tearDown() override {
        if (saw_async || saw_sync) {
            EXPECT_TRUE(saw_async);
            EXPECT_TRUE(saw_sync);
            EXPECT_EQ(value, 401);
        }
        torn_down = true;
    }
};

struct [[using gentest: fixture(suite)]] GroupFenceSuiteFixture {};
struct [[using gentest: fixture(suite)]] GroupFenceFirstSuiteFixture {};
struct [[using gentest: fixture(suite)]] GroupFenceSecondSuiteFixture {};

struct LocalAsyncThrowingTeardownFixture : gentest::AsyncFixtureTearDown {
    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        throw std::runtime_error("async-teardown-secondary-marker");
    }
};

struct LocalAsyncFailingUnresumableTeardownFixture : gentest::AsyncFixtureTearDown {
    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        EXPECT_TRUE(false, "async-local-unresumable-teardown-marker");
    }
};

[[using gentest: test("batch/server")]]
gentest::async_test<void> server();

[[using gentest: test("batch/client")]]
gentest::async_test<void> client();

[[using gentest: test("live_demo/00_suspended")]]
gentest::async_test<void> live_demo_suspended();

[[using gentest: test("live_demo/01_driver")]]
gentest::async_test<void> live_demo_driver();

[[using gentest: test("fail_fast/00_async_fail")]]
gentest::async_test<void> fail_fast_async_fail();

[[using gentest: test("fail_fast/01_sync_should_not_run")]]
void fail_fast_sync_should_not_run();

[[using gentest: test("fail_fast_snapshot/00_async_fail_after_release")]]
gentest::async_test<void> fail_fast_snapshot_async_fail_after_release();

[[using gentest: test("fail_fast_snapshot/01_async_should_not_run_after_failure")]]
gentest::async_test<void> fail_fast_snapshot_async_should_not_run_after_failure();

[[using gentest: test("fail_fast_snapshot/02_release")]]
void fail_fast_snapshot_release_waiters();

[[using gentest: test("fail_fast_self_adopted/00_async_fails_with_adopted_worker")]]
gentest::async_test<void> fail_fast_self_adopted_async_fails_with_adopted_worker();

[[using gentest: test("fail_fast_self_adopted/01_sync_should_not_run")]]
void fail_fast_self_adopted_sync_should_not_run();

[[using gentest: test("fail_fast_done_adopted_late/00_done_with_late_worker_log")]]
gentest::async_test<void> fail_fast_done_adopted_late_done_with_late_worker_log();

[[using gentest: test("fail_fast_done_adopted_late/01_sync_failure")]]
void fail_fast_done_adopted_late_sync_failure();

[[using gentest: test("fail_fast_done_adopted_late/02_sync_should_not_run")]]
void fail_fast_done_adopted_late_sync_should_not_run();

[[using gentest: test("fail_fast_cancel_adopted_context/00_pending_worker_logs_after_cancel")]]
gentest::async_test<void> fail_fast_cancel_adopted_context_pending_worker_logs_after_cancel();

[[using gentest: test("fail_fast_cancel_adopted_context/01_sync_failure")]]
void fail_fast_cancel_adopted_context_sync_failure();

[[using gentest: test("fail_fast_cancel_released_context/00_pending_worker_reuses_released_context"), death]]
gentest::async_test<void> fail_fast_cancel_released_context_pending_worker_reuses_released_context();

[[using gentest: test("fail_fast_cancel_released_context/01_sync_failure")]]
void fail_fast_cancel_released_context_sync_failure();

[[using gentest: test("fail_fast_cancel_local_fixture/00_pending")]]
gentest::async_test<void> fail_fast_cancel_local_fixture_pending(FailFastCancelLocalFixture &);

[[using gentest: test("fail_fast_cancel_local_fixture/01_sync_failure")]]
void fail_fast_cancel_local_fixture_sync_failure();

[[using gentest: test("fail_fast_cancel_adopted_local_fixture/00_pending")]]
gentest::async_test<void> fail_fast_cancel_adopted_local_fixture_pending(FailFastCancelAdoptedLocalFixture &);

[[using gentest: test("fail_fast_cancel_adopted_local_fixture/01_sync_failure")]]
void fail_fast_cancel_adopted_local_fixture_sync_failure();

[[using gentest: test("fail_fast_cancel_slow_adopted/00_pending_needs_resume")]]
gentest::async_test<void> fail_fast_cancel_slow_adopted_pending_needs_resume();

[[using gentest: test("fail_fast_cancel_slow_adopted/01_sync_failure")]]
void fail_fast_cancel_slow_adopted_sync_failure();

[[using gentest: test("fail_fast_cancel_adopted_skip/00_pending_worker_logs_after_cancel")]]
gentest::async_test<void> fail_fast_cancel_adopted_skip_pending_worker_logs_after_cancel();

[[using gentest: test("fail_fast_cancel_adopted_skip/01_sync_failure")]]
void fail_fast_cancel_adopted_skip_sync_failure();

[[using gentest: test("fail_fast_final_drain/00_async_fail_after_adopted_release")]]
gentest::async_test<void> fail_fast_final_drain_async_fail_after_adopted_release();

[[using gentest: test("fail_fast_final_drain/01_async_should_not_run_after_failure")]]
gentest::async_test<void> fail_fast_final_drain_async_should_not_run_after_failure();

[[using gentest: test("timer/00_sleep_for_waits")]]
gentest::async_test<void> timer_sleep_for_waits();

[[using gentest: test("timer/01_yield_runs_while_sleep_pending")]]
gentest::async_test<void> timer_yield_runs_while_sleep_pending();

[[using gentest: test("timer/02_sleep_until_waits")]]
gentest::async_test<void> timer_sleep_until_waits();

[[using gentest: test("timer_stress/00_ready_wins_expired_timeout")]]
gentest::async_test<void> timer_stress_ready_wins_expired_timeout();

[[using gentest: test("timer_stress/01_timeout_cancels_late_event_waiter")]]
gentest::async_test<void> timer_stress_timeout_cancels_late_event_waiter();

[[using gentest: test("timer_stress/02_timeout_cancels_inner_sleep_timer")]]
gentest::async_test<void> timer_stress_timeout_cancels_inner_sleep_timer();

auto timer_stress_simultaneous_sleeper() -> gentest::async_test<void>;

[[using gentest: test("timer_stress/03_simultaneous_driver")]]
gentest::async_test<void> timer_stress_simultaneous_driver();

[[using gentest: test("timer_stress/04_simultaneous_sleeper_a")]]
gentest::async_test<void> timer_stress_simultaneous_sleeper_a();

[[using gentest: test("timer_stress/05_simultaneous_sleeper_b")]]
gentest::async_test<void> timer_stress_simultaneous_sleeper_b();

[[using gentest: test("timer_stress/06_simultaneous_sleeper_c")]]
gentest::async_test<void> timer_stress_simultaneous_sleeper_c();

[[using gentest: test("timer_stress/07_simultaneous_sleeper_d")]]
gentest::async_test<void> timer_stress_simultaneous_sleeper_d();

[[using gentest: test("wait_for/event_ready_before_timeout")]]
gentest::async_test<void> wait_for_event_ready_before_timeout();

[[using gentest: test("wait_for/event_timeout_late_signal")]]
gentest::async_test<void> wait_for_event_timeout_late_signal();

[[using gentest: test("wait_for/future_void_ready")]]
gentest::async_test<void> wait_for_future_void_ready();

[[using gentest: test("wait_for/future_value_timeout")]]
gentest::async_test<void> wait_for_future_value_timeout();

[[using gentest: test("wait_for/future_move_only_ready")]]
gentest::async_test<void> wait_for_future_move_only_ready();

[[using gentest: test("wait_for/async_test_pass")]]
gentest::async_test<void> wait_for_async_test_pass();

[[using gentest: test("wait_for/async_test_value")]]
gentest::async_test<void> wait_for_async_test_value();

[[using gentest: test("wait_for/async_test_exception")]]
gentest::async_test<void> wait_for_async_test_exception();

[[using gentest: test("wait_for/async_test_timeout_cancels_stale_waiter")]]
gentest::async_test<void> wait_for_async_test_timeout_cancels_stale_waiter();

[[using gentest: test("wait_for/async_test_timeout_cancels_nested_stale_waiter")]]
gentest::async_test<void> wait_for_async_test_timeout_cancels_nested_stale_waiter();

[[using gentest: test("lvalue_await/00_first_waiter")]]
gentest::async_test<void> lvalue_await_first_waiter();

[[using gentest: test("lvalue_await/01_concurrent_waiter")]]
gentest::async_test<void> lvalue_await_concurrent_waiter();

[[using gentest: test("lvalue_await/02_completed_reawait")]]
gentest::async_test<void> lvalue_await_completed_task();

[[using gentest: test("value/discard")]]
gentest::async_test<int> value_discard();

[[using gentest: test("case_api/sync_fn_async_guard")]]
void case_api_sync_fn_async_guard();

[[using gentest: test("fixture/local_async")]]
gentest::async_test<void> local_async_fixture(LocalAsyncFixture &fixture);

[[using gentest: test("fixture/mixed")]]
gentest::async_test<void> mixed_fixtures(LocalSyncFixture &sync_fixture, LocalAsyncFixture &async_fixture);

[[using gentest: test("parameterized/value"), parameters(value, 1, 2, 3)]]
gentest::async_test<void> parameterized_value(int value);

template <typename T>
[[using gentest: test("template/type"), template(T, int, long)]]
inline gentest::async_test<void> template_type() {
    co_await gentest::async::yield();
    EXPECT_TRUE(std::is_integral_v<T>);
}

[[using gentest: test("fixture/local_lifecycle/00_async_use")]]
gentest::async_test<void> local_async_lifecycle_use(LocalAsyncLifecycleFixture &fixture);

[[using gentest: test("fixture/shared_async_adopted_setup")]]
void shared_async_adopted_setup(BlockingSchedulerAdoptedFixture &fixture);

[[using gentest: test("fixture/local_teardown_dual_failure")]]
gentest::async_test<void> local_async_teardown_preserves_primary_failure(LocalAsyncThrowingTeardownFixture &);

[[using gentest: test("fixture/local_unresumable_teardown/00_async_never")]]
gentest::async_test<void> local_async_unresumable_reports_teardown(LocalAsyncFailingUnresumableTeardownFixture &);

[[using gentest: test("fixture/shared_suite/00_async_wait")]]
gentest::async_test<void> shared_suite_async_wait(SharedSuiteAsyncFixture &fixture);

[[using gentest: test("fixture/shared_suite/01_sync_release")]]
void shared_suite_sync_release(SharedSuiteAsyncFixture &fixture);

[[using gentest: test("fixture/shared_suite/02_sync_check")]]
void shared_suite_sync_check(SharedSuiteAsyncFixture &fixture);

[[using gentest: test("fixture/shared_global/00_sync_seed")]]
void shared_global_sync_seed(SharedGlobalAsyncFixture &fixture);

[[using gentest: test("fixture/shared_global/01_async_wait")]]
gentest::async_test<void> shared_global_async_wait(SharedGlobalAsyncHandle fixture);

[[using gentest: test("fixture/shared_global/02_sync_release")]]
void shared_global_sync_release(SharedGlobalAsyncRawAlias fixture);

[[using gentest: test("fixture/shared_global/03_sync_check")]]
void shared_global_sync_check(SharedGlobalAsyncFixture &fixture);

[[using gentest: test("fixture/shared_suite_unresumable/00_async_never")]]
gentest::async_test<void> shared_suite_unresumable_never(SharedSuiteUnresumableFixture &fixture);

[[using gentest: test("fixture/shared_suite_unresumable/01_sync_after_never")]]
void shared_suite_unresumable_sync_after(SharedSuiteUnresumableFixture &fixture);

[[using gentest: test("fixture/shared_global_unresumable/00_async_never")]]
gentest::async_test<void> shared_global_unresumable_never(SharedGlobalUnresumableFixture &fixture);

[[using gentest: test("fixture/shared_global_unresumable/01_sync_after_never")]]
void shared_global_unresumable_sync_after(SharedGlobalUnresumableFixture &fixture);

[[using gentest: test("fixture/group_fence/00_async_wait")]]
gentest::async_test<void> fixture_group_fence_async_wait();

[[using gentest: test("fixture/group_fence/01_suite_release")]]
void fixture_group_fence_suite_release(LocalSyncFixture &local_fixture, GroupFenceSuiteFixture &);

[[using gentest: test("fixture/group_fence/02_suite_check")]]
void fixture_group_fence_suite_check(GroupFenceSuiteFixture &);

[[using gentest: test("fixture/group_fence_suite/00_async_wait")]]
gentest::async_test<void> fixture_suite_group_fence_async_wait(GroupFenceFirstSuiteFixture &);

[[using gentest: test("fixture/group_fence_suite/01_second_release")]]
void fixture_suite_group_fence_second_release(GroupFenceSecondSuiteFixture &);

[[using gentest: test("fixture/group_fence_suite/02_second_check")]]
void fixture_suite_group_fence_second_check(GroupFenceSecondSuiteFixture &);

[[using gentest: test("adopted_resume/worker_releases")]]
gentest::async_test<void> adopted_worker_releases();

[[using gentest: test("adopted_resume/worker_waits_for_resumed_ack")]]
gentest::async_test<void> adopted_worker_waits_for_resumed_ack();

[[using gentest: test("promise/order/00_waiter")]]
gentest::async_test<void> promise_first_terminal_waiter();

[[using gentest: test("promise/order/01_driver")]]
gentest::async_test<void> promise_first_terminal_driver();

[[using gentest: test("promise/pre_blocked_before_wait")]]
gentest::async_test<void> promise_pre_blocked_before_wait();

[[using gentest: test("blocked/explicit/00_waiter")]]
gentest::async_test<void> explicit_blocked_waiter();

[[using gentest: test("blocked/explicit/01_driver")]]
gentest::async_test<void> explicit_blocked_driver();

[[using gentest: test("blocked/after_failure")]]
gentest::async_test<void> blocked_after_failure_reports_failure();

[[using gentest: test("blocked/empty_reason/00_waiter")]]
gentest::async_test<void> empty_reason_blocked_waiter();

[[using gentest: test("blocked/empty_reason/01_driver")]]
gentest::async_test<void> empty_reason_blocked_driver();

[[using gentest: test("outcome/xfail_expect_fail_after_suspend")]]
gentest::async_test<void> outcome_xfail_expect_fail_after_suspend();

[[using gentest: test("outcome/xfail_throw_after_suspend")]]
gentest::async_test<void> outcome_xfail_throw_after_suspend();

[[using gentest: test("outcome/xfail_xpass_after_suspend")]]
gentest::async_test<void> outcome_xfail_xpass_after_suspend();

[[using gentest: test("outcome/skip_overrides_xfail_after_suspend")]]
gentest::async_test<void> outcome_skip_overrides_xfail_after_suspend();

[[using gentest: test("outcome/runtime_skip_after_suspend")]]
gentest::async_test<void> outcome_runtime_skip_after_suspend();

[[using gentest: test("outcome/skip_after_failure_is_fail_after_suspend")]]
gentest::async_test<void> outcome_skip_after_failure_is_fail_after_suspend();

[[using gentest: test("waiter_lifetime/process_exit_unresumable")]]
gentest::async_test<void> waiter_lifetime_process_exit_unresumable();

[[using gentest: test("blocked/never")]]
gentest::async_test<void> blocked_never();

[[using gentest: test("yield/unsupported_suspend_after_yield")]]
gentest::async_test<void> unsupported_suspend_after_yield();

} // namespace async
