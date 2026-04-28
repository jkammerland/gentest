#include "gentest/attributes.h"
#include "gentest/detail/runtime_context.h"
#include "gentest/runner.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace gentest::asserts;

namespace async {

gentest::async::manual_event server_ready;
gentest::async::manual_event client_done;
std::vector<std::string>     order;

void reset_batch_if_previous_round_completed() {
    if (server_ready.is_set() && client_done.is_set()) {
        server_ready.reset();
        client_done.reset();
        order.clear();
    }
}

gentest::async::manual_event live_demo_resume;
std::vector<std::string>     live_demo_order;

gentest::async::manual_event fail_fast_snapshot_release;
gentest::async::manual_event fail_fast_cancel_adopted_resume;
gentest::async::manual_event fail_fast_cancel_adopted_context_resume;
gentest::async::manual_event fail_fast_cancel_local_fixture_resume;
gentest::async::manual_event fail_fast_cancel_adopted_local_fixture_resume;
gentest::async::manual_event fail_fast_cancel_adopted_skip_resume;
gentest::async::manual_event fail_fast_final_drain_fail_release;
gentest::async::manual_event fail_fast_final_drain_late_release;
std::atomic<int>             fail_fast_final_drain_waiters{0};
std::atomic<bool>            fail_fast_cancel_adopted_context_worker_started{false};
std::atomic<bool>            fail_fast_cancel_adopted_context_worker_done{true};
std::atomic<bool>            fail_fast_cancel_released_context_worker_started{false};
std::atomic<bool>            fail_fast_cancel_released_context_worker_done{true};
std::atomic<bool>            fail_fast_cancel_adopted_local_fixture_started{false};
std::atomic<bool>            fail_fast_cancel_adopted_local_fixture_worker_done{true};
std::atomic<bool>            fail_fast_cancel_adopted_local_fixture_torn_down{true};
std::atomic<bool>            fail_fast_cancel_adopted_skip_worker_started{false};
std::atomic<bool>            fail_fast_cancel_adopted_skip_worker_done{true};

gentest::async::manual_event group_fence_release;
std::vector<std::string>     group_fence_order;

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

gentest::async::manual_event suite_group_fence_release;
std::vector<std::string>     suite_group_fence_order;

gentest::async::manual_event process_exit_event;

struct ProcessExitSignal {
    ~ProcessExitSignal() { process_exit_event.set(); }
};

ProcessExitSignal process_exit_signal;

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

FailFastCancelAdoptedContextExitWait fail_fast_cancel_adopted_context_exit_wait;

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

FailFastCancelReleasedContextExitWait fail_fast_cancel_released_context_exit_wait;

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
    }
};

FailFastCancelAdoptedLocalFixtureExitWait fail_fast_cancel_adopted_local_fixture_exit_wait;

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

FailFastCancelAdoptedSkipExitWait fail_fast_cancel_adopted_skip_exit_wait;

gentest::async::manual_event                       adopted_resume_event;
std::shared_ptr<gentest::async::completion_source> completion_order_source;
std::shared_ptr<gentest::async::completion_source> explicit_blocked_source;
std::shared_ptr<gentest::async::completion_source> empty_blocked_source;

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
        release_async.reset();
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
        release_async.reset();
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

        JoiningThread worker(std::thread(
            [context = std::move(context), started = std::move(started), &release, resumed_ready = std::move(resumed_ready)]() mutable {
                auto adoption = gentest::set_current_context(context);
                started->set_value();
                release.set();
                EXPECT_TRUE(resumed_ready.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
            }));

        ready.wait();
        co_await release.wait("adopted fixture setup should wake scheduler before releasing context");
        resumed.set_value();
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
        never.reset();
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
        never.reset();
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

[[using gentest: test("batch/server")]]
gentest::async_test<void> server() {
    reset_batch_if_previous_round_completed();
    order.clear();
    co_await gentest::async::yield();
    order.emplace_back("server");
    server_ready.set();
    co_await client_done.wait("client did not finish");
    ASSERT_EQ(order.size(), std::size_t{2});
    EXPECT_EQ(order[0], "server");
    EXPECT_EQ(order[1], "client");
}

[[using gentest: test("batch/client")]]
gentest::async_test<void> client() {
    reset_batch_if_previous_round_completed();
    co_await server_ready.wait("server was not selected");
    order.emplace_back("client");
    client_done.set();
    ASSERT_EQ(order.size(), std::size_t{2});
    EXPECT_EQ(order[0], "server");
    EXPECT_EQ(order[1], "client");
}

[[using gentest: test("live_demo/00_suspended")]]
gentest::async_test<void> live_demo_suspended() {
    live_demo_order.clear();
    live_demo_resume.reset();
    live_demo_order.emplace_back("suspended:start");

    co_await live_demo_resume.wait("waiting for live demo driver");

    live_demo_order.emplace_back("suspended:resumed");
    ASSERT_EQ(live_demo_order.size(), std::size_t{3});
    EXPECT_EQ(live_demo_order[0], "suspended:start");
    EXPECT_EQ(live_demo_order[1], "driver:complete");
    EXPECT_EQ(live_demo_order[2], "suspended:resumed");
}

[[using gentest: test("live_demo/01_driver")]]
gentest::async_test<void> live_demo_driver() {
    for (int i = 0; i < kLiveDemoRounds; ++i) {
        co_await gentest::async::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
    }

    live_demo_order.emplace_back("driver:complete");
    live_demo_resume.set();
}

[[using gentest: test("fail_fast/00_async_fail")]]
gentest::async_test<void> fail_fast_async_fail() {
    co_await gentest::async::yield();
    EXPECT_TRUE(false);
}

[[using gentest: test("fail_fast/01_sync_should_not_run")]]
void fail_fast_sync_should_not_run() {
    gentest::fail("fail-fast allowed a later sync case to run");
}

[[using gentest: test("fail_fast_snapshot/00_async_fail_after_release")]]
gentest::async_test<void> fail_fast_snapshot_async_fail_after_release() {
    fail_fast_snapshot_release.reset();
    co_await fail_fast_snapshot_release.wait("driver did not release fail-fast snapshot cases");
    EXPECT_TRUE(false);
}

[[using gentest: test("fail_fast_snapshot/01_async_should_not_run_after_failure")]]
gentest::async_test<void> fail_fast_snapshot_async_should_not_run_after_failure() {
    co_await fail_fast_snapshot_release.wait("driver did not release fail-fast snapshot cases");
    gentest::fail("fail-fast allowed a later ready async case to run");
}

[[using gentest: test("fail_fast_snapshot/02_release")]]
void fail_fast_snapshot_release_waiters() {
    fail_fast_snapshot_release.set();
}

[[using gentest: test("fail_fast_cancel_adopted/00_pending_needs_resume")]]
gentest::async_test<void> fail_fast_cancel_adopted_pending_needs_resume() {
    fail_fast_cancel_adopted_resume.reset();

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::promise<void> resumed;
    auto               resumed_ready = resumed.get_future();
    JoiningThread      worker(
        std::thread([context = std::move(context), started = std::move(started), resumed_ready = std::move(resumed_ready)]() mutable {
            auto adoption = gentest::set_current_context(context);
            started->set_value();
            resumed_ready.wait();
        }));

    ready.wait();
    co_await fail_fast_cancel_adopted_resume.wait("fail-fast cancellation resume should remain queued");
    resumed.set_value();
    gentest::fail("fail-fast cancellation resumed pending adopted case");
}

[[using gentest: test("fail_fast_cancel_adopted/01_failure_releases_pending")]]
void fail_fast_cancel_adopted_failure_releases_pending() {
    fail_fast_cancel_adopted_resume.set();
    EXPECT_TRUE(false);
}

[[using gentest: test("fail_fast_cancel_adopted_context/00_pending_worker_logs_after_cancel")]]
gentest::async_test<void> fail_fast_cancel_adopted_context_pending_worker_logs_after_cancel() {
    fail_fast_cancel_adopted_context_resume.reset();
    fail_fast_cancel_adopted_context_worker_started.store(false, std::memory_order_release);
    fail_fast_cancel_adopted_context_worker_done.store(false, std::memory_order_release);

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto adoption = gentest::set_current_context(context);
        fail_fast_cancel_adopted_context_worker_started.store(true, std::memory_order_release);
        started->set_value();
        while (context && context->active.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        gentest::log("adopted worker logged after fail-fast cancellation");
        EXPECT_TRUE(false, "adopted worker recorded failure after fail-fast cancellation");
        fail_fast_cancel_adopted_context_worker_done.store(true, std::memory_order_release);
    }).detach();

    ready.wait();
    co_await fail_fast_cancel_adopted_context_resume.wait("fail-fast cancellation should leave adopted context usable");
    gentest::fail("fail-fast cancellation resumed pending adopted context case");
}

[[using gentest: test("fail_fast_cancel_adopted_context/01_sync_failure")]]
void fail_fast_cancel_adopted_context_sync_failure() {
    EXPECT_TRUE(false, "fail-fast sync failure should not make adopted context operations fatal");
}

[[using gentest: test("fail_fast_cancel_released_context/00_pending_worker_reuses_released_context")]]
gentest::async_test<void> fail_fast_cancel_released_context_pending_worker_reuses_released_context() {
    fail_fast_cancel_adopted_context_resume.reset();
    fail_fast_cancel_released_context_worker_started.store(false, std::memory_order_release);
    fail_fast_cancel_released_context_worker_done.store(false, std::memory_order_release);

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        {
            auto adoption = gentest::set_current_context(context);
            fail_fast_cancel_released_context_worker_started.store(true, std::memory_order_release);
            started->set_value();
            while (context && context->active.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            gentest::log("adopted worker logged during fail-fast cancellation");
        }

        auto stale_adoption = gentest::set_current_context(context);
        gentest::log("stale adopted worker logged after releasing fail-fast cancellation context");
        fail_fast_cancel_released_context_worker_done.store(true, std::memory_order_release);
    }).detach();

    ready.wait();
    co_await fail_fast_cancel_adopted_context_resume.wait("fail-fast cancellation should close released adopted context");
    gentest::fail("fail-fast cancellation resumed released context case");
}

[[using gentest: test("fail_fast_cancel_released_context/01_sync_failure")]]
void fail_fast_cancel_released_context_sync_failure() {
    EXPECT_TRUE(false, "fail-fast sync failure should close adopted contexts after release");
}

[[using gentest: test("fail_fast_cancel_local_fixture/00_pending")]]
gentest::async_test<void> fail_fast_cancel_local_fixture_pending(FailFastCancelLocalFixture &) {
    fail_fast_cancel_local_fixture_resume.reset();
    co_await fail_fast_cancel_local_fixture_resume.wait("fail-fast cancellation should run async local fixture teardown");
    gentest::fail("fail-fast cancellation resumed pending local fixture case");
}

[[using gentest: test("fail_fast_cancel_local_fixture/01_sync_failure")]]
void fail_fast_cancel_local_fixture_sync_failure() {
    EXPECT_TRUE(false, "fail-fast sync failure should cancel pending async local fixture with teardown");
}

[[using gentest: test("fail_fast_cancel_adopted_local_fixture/00_pending")]]
gentest::async_test<void> fail_fast_cancel_adopted_local_fixture_pending(FailFastCancelAdoptedLocalFixture &) {
    fail_fast_cancel_adopted_local_fixture_resume.reset();
    fail_fast_cancel_adopted_local_fixture_started.store(false, std::memory_order_release);
    fail_fast_cancel_adopted_local_fixture_worker_done.store(false, std::memory_order_release);
    fail_fast_cancel_adopted_local_fixture_torn_down.store(false, std::memory_order_release);

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto adoption = gentest::set_current_context(context);
        fail_fast_cancel_adopted_local_fixture_started.store(true, std::memory_order_release);
        started->set_value();
        while (context && context->active.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        gentest::log("adopted worker observed fail-fast local fixture cancellation");
        fail_fast_cancel_adopted_local_fixture_worker_done.store(true, std::memory_order_release);
    }).detach();

    ready.wait();
    co_await fail_fast_cancel_adopted_local_fixture_resume.wait("fail-fast cancellation should run adopted async local fixture teardown");
    gentest::fail("fail-fast cancellation resumed adopted local fixture case");
}

[[using gentest: test("fail_fast_cancel_adopted_local_fixture/01_sync_failure")]]
void fail_fast_cancel_adopted_local_fixture_sync_failure() {
    EXPECT_TRUE(false, "fail-fast sync failure should trigger adopted async local fixture teardown");
}

[[using gentest: test("fail_fast_cancel_adopted_skip/00_pending_worker_skips_after_cancel")]]
gentest::async_test<void> fail_fast_cancel_adopted_skip_pending_worker_skips_after_cancel() {
    fail_fast_cancel_adopted_skip_resume.reset();
    fail_fast_cancel_adopted_skip_worker_started.store(false, std::memory_order_release);
    fail_fast_cancel_adopted_skip_worker_done.store(false, std::memory_order_release);

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto adoption = gentest::set_current_context(context);
        fail_fast_cancel_adopted_skip_worker_started.store(true, std::memory_order_release);
        started->set_value();
        while (context && context->active.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        try {
            gentest::skip("adopted worker skipped after fail-fast cancellation");
        } catch (...) {}
        fail_fast_cancel_adopted_skip_worker_done.store(true, std::memory_order_release);
    }).detach();

    ready.wait();
    co_await fail_fast_cancel_adopted_skip_resume.wait("fail-fast cancellation should leave adopted skip context usable");
    gentest::fail("fail-fast cancellation resumed adopted skip case");
}

[[using gentest: test("fail_fast_cancel_adopted_skip/01_sync_failure")]]
void fail_fast_cancel_adopted_skip_sync_failure() {
    EXPECT_TRUE(false, "fail-fast sync failure should not make adopted skip fatal");
}

[[using gentest: test("fail_fast_final_drain/00_async_fail_after_adopted_release")]]
gentest::async_test<void> fail_fast_final_drain_async_fail_after_adopted_release() {
    fail_fast_final_drain_fail_release.reset();
    fail_fast_final_drain_late_release.reset();
    fail_fast_final_drain_waiters.store(0, std::memory_order_release);

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    JoiningThread worker(std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto adoption = gentest::set_current_context(context);
        started->set_value();
        while (fail_fast_final_drain_waiters.load(std::memory_order_acquire) < 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        fail_fast_final_drain_fail_release.set();
        fail_fast_final_drain_late_release.set();
    }));
    ready.wait();

    fail_fast_final_drain_waiters.fetch_add(1, std::memory_order_acq_rel);
    co_await fail_fast_final_drain_fail_release.wait("final drain did not release fail-fast failure");
    EXPECT_TRUE(false);
}

[[using gentest: test("fail_fast_final_drain/01_async_should_not_run_after_failure")]]
gentest::async_test<void> fail_fast_final_drain_async_should_not_run_after_failure() {
    fail_fast_final_drain_waiters.fetch_add(1, std::memory_order_acq_rel);
    co_await fail_fast_final_drain_late_release.wait("final drain did not release later ready case");
    gentest::fail("fail-fast final drain allowed a later ready async case to run");
}

[[using gentest: test("value/discard")]]
gentest::async_test<int> value_discard() {
    co_await gentest::async::yield();
    EXPECT_TRUE(true);
    co_return 7;
}

[[using gentest: test("case_api/sync_fn_async_guard")]]
void case_api_sync_fn_async_guard() {
    auto cases = gentest::registered_cases();
    auto it    = std::ranges::find_if(cases, [](const gentest::Case &test_case) { return test_case.name == "async/value/discard"; });
    ASSERT_TRUE(it != cases.end());
    ASSERT_TRUE(it->is_async);
    ASSERT_TRUE(it->async_fn != nullptr);
    it->fn(nullptr);
}

[[using gentest: test("fixture/local_async")]]
gentest::async_test<void> local_async_fixture(LocalAsyncFixture &fixture) {
    EXPECT_EQ(fixture.value, 42);
    co_return;
}

[[using gentest: test("fixture/mixed")]]
gentest::async_test<void> mixed_fixtures(LocalSyncFixture &sync_fixture, LocalAsyncFixture &async_fixture) {
    EXPECT_EQ(sync_fixture.value, 7);
    EXPECT_EQ(async_fixture.value, 42);
    co_return;
}

[[using gentest: test("fixture/local_lifecycle/00_async_use")]]
gentest::async_test<void> local_async_lifecycle_use(LocalAsyncLifecycleFixture &fixture) {
    EXPECT_EQ(fixture.value, 123);
    EXPECT_TRUE(fixture.setup);
    EXPECT_FALSE(fixture.torn_down);
    co_return;
}

[[using gentest: test("fixture/shared_async_adopted_setup")]]
void shared_async_adopted_setup(BlockingSchedulerAdoptedFixture &fixture) {
    EXPECT_TRUE(fixture.setup);
}

[[using gentest: test("fixture/local_teardown_dual_failure")]]
gentest::async_test<void> local_async_teardown_preserves_primary_failure(LocalAsyncThrowingTeardownFixture &) {
    co_await gentest::async::yield();
    throw std::runtime_error("async-body-primary-marker");
}

[[using gentest: test("fixture/shared_suite/00_async_wait")]]
gentest::async_test<void> shared_suite_async_wait(SharedSuiteAsyncFixture &fixture) {
    if (!SharedSuiteAsyncFixture::first) {
        SharedSuiteAsyncFixture::first = &fixture;
    }
    EXPECT_EQ(&fixture, SharedSuiteAsyncFixture::first);
    EXPECT_EQ(SharedSuiteAsyncFixture::setups, 1);
    fixture.value     = 10;
    fixture.saw_async = false;
    fixture.saw_sync  = false;
    fixture.saw_check = false;
    fixture.release_async.reset();
    fixture.events.clear();
    EXPECT_EQ(fixture.value, 10);
    fixture.saw_async = true;
    fixture.events.emplace_back("async:start");

    co_await fixture.release_async.wait("suite sync case did not release async case");

    EXPECT_EQ(fixture.value, 20);
    fixture.events.emplace_back("async:done");
}

[[using gentest: test("fixture/shared_suite/01_sync_release")]]
void shared_suite_sync_release(SharedSuiteAsyncFixture &fixture) {
    EXPECT_EQ(&fixture, SharedSuiteAsyncFixture::first);
    EXPECT_EQ(SharedSuiteAsyncFixture::setups, 1);
    EXPECT_EQ(fixture.value, 10);
    fixture.saw_sync = true;
    fixture.value    = 20;
    fixture.events.emplace_back("sync:release");
    fixture.release_async.set();
}

[[using gentest: test("fixture/shared_suite/02_sync_check")]]
void shared_suite_sync_check(SharedSuiteAsyncFixture &fixture) {
    EXPECT_EQ(&fixture, SharedSuiteAsyncFixture::first);
    fixture.saw_check = true;
    EXPECT_TRUE(fixture.saw_async);
    EXPECT_TRUE(fixture.saw_sync);
    ASSERT_EQ(fixture.events.size(), std::size_t{3});
    EXPECT_EQ(fixture.events[0], "async:start");
    EXPECT_EQ(fixture.events[1], "sync:release");
    EXPECT_EQ(fixture.events[2], "async:done");
}

[[using gentest: test("fixture/shared_global/00_sync_seed")]]
void shared_global_sync_seed(SharedGlobalAsyncFixture &fixture) {
    if (!SharedGlobalAsyncFixture::first) {
        SharedGlobalAsyncFixture::first = &fixture;
    }
    EXPECT_EQ(&fixture, SharedGlobalAsyncFixture::first);
    EXPECT_EQ(SharedGlobalAsyncFixture::setups, 1);
    fixture.value     = 100;
    fixture.saw_seed  = false;
    fixture.saw_async = false;
    fixture.saw_sync  = false;
    fixture.saw_check = false;
    fixture.release_async.reset();
    fixture.events.clear();
    EXPECT_EQ(fixture.value, 100);
    fixture.saw_seed = true;
    fixture.events.emplace_back("sync:seed");
}

[[using gentest: test("fixture/shared_global/01_async_wait")]]
gentest::async_test<void> shared_global_async_wait(SharedGlobalAsyncHandle fixture) {
    ASSERT_TRUE(static_cast<bool>(fixture));
    EXPECT_EQ(fixture.get(), SharedGlobalAsyncFixture::first);
    EXPECT_EQ(SharedGlobalAsyncFixture::setups, 1);
    EXPECT_TRUE(fixture->saw_seed);
    EXPECT_EQ(fixture->value, 100);
    fixture->saw_async = true;
    fixture->events.emplace_back("async:start");

    co_await fixture->release_async.wait("global sync case did not release async case");

    EXPECT_EQ(fixture->value, 200);
    fixture->events.emplace_back("async:done");
}

[[using gentest: test("fixture/shared_global/02_sync_release")]]
void shared_global_sync_release(SharedGlobalAsyncRawAlias fixture) {
    ASSERT_TRUE(fixture != nullptr);
    EXPECT_EQ(fixture, SharedGlobalAsyncFixture::first);
    EXPECT_TRUE(fixture->saw_seed);
    EXPECT_TRUE(fixture->saw_async);
    fixture->saw_sync = true;
    fixture->value    = 200;
    fixture->events.emplace_back("sync:release");
    fixture->release_async.set();
}

[[using gentest: test("fixture/shared_global/03_sync_check")]]
void shared_global_sync_check(SharedGlobalAsyncFixture &fixture) {
    EXPECT_EQ(&fixture, SharedGlobalAsyncFixture::first);
    fixture.saw_check = true;
    EXPECT_TRUE(fixture.saw_seed);
    EXPECT_TRUE(fixture.saw_async);
    EXPECT_TRUE(fixture.saw_sync);
    ASSERT_EQ(fixture.events.size(), std::size_t{4});
    EXPECT_EQ(fixture.events[0], "sync:seed");
    EXPECT_EQ(fixture.events[1], "async:start");
    EXPECT_EQ(fixture.events[2], "sync:release");
    EXPECT_EQ(fixture.events[3], "async:done");
}

[[using gentest: test("fixture/shared_suite_unresumable/00_async_never")]]
gentest::async_test<void> shared_suite_unresumable_never(SharedSuiteUnresumableFixture &fixture) {
    EXPECT_EQ(fixture.value, 300);
    fixture.saw_async = true;
    co_await fixture.never.wait("suite fixture resume handle was not signalled");
}

[[using gentest: test("fixture/shared_suite_unresumable/01_sync_after_never")]]
void shared_suite_unresumable_sync_after(SharedSuiteUnresumableFixture &fixture) {
    EXPECT_TRUE(fixture.saw_async);
    fixture.saw_sync = true;
    fixture.value    = 301;
}

[[using gentest: test("fixture/shared_global_unresumable/00_async_never")]]
gentest::async_test<void> shared_global_unresumable_never(SharedGlobalUnresumableFixture &fixture) {
    EXPECT_EQ(fixture.value, 400);
    fixture.saw_async = true;
    co_await fixture.never.wait("global fixture resume handle was not signalled");
}

[[using gentest: test("fixture/shared_global_unresumable/01_sync_after_never")]]
void shared_global_unresumable_sync_after(SharedGlobalUnresumableFixture &fixture) {
    EXPECT_TRUE(fixture.saw_async);
    fixture.saw_sync = true;
    fixture.value    = 401;
}

[[using gentest: test("fixture/group_fence/00_async_wait")]]
gentest::async_test<void> fixture_group_fence_async_wait() {
    group_fence_order.clear();
    group_fence_release.reset();
    group_fence_order.emplace_back("async:start");
    co_await group_fence_release.wait("fixture group boundary was crossed");
    group_fence_order.emplace_back("async:resumed");
}

[[using gentest: test("fixture/group_fence/01_suite_release")]]
void fixture_group_fence_suite_release(LocalSyncFixture &local_fixture, GroupFenceSuiteFixture &) {
    EXPECT_EQ(local_fixture.value, 7);
    ASSERT_EQ(group_fence_order.size(), std::size_t{1});
    EXPECT_EQ(group_fence_order[0], "async:start");
    group_fence_order.emplace_back("suite:release");
    group_fence_release.set();
}

[[using gentest: test("fixture/group_fence/02_suite_check")]]
void fixture_group_fence_suite_check(GroupFenceSuiteFixture &) {
    ASSERT_EQ(group_fence_order.size(), std::size_t{2});
    EXPECT_EQ(group_fence_order[0], "async:start");
    EXPECT_EQ(group_fence_order[1], "suite:release");
}

[[using gentest: test("fixture/group_fence_suite/00_async_wait")]]
gentest::async_test<void> fixture_suite_group_fence_async_wait(GroupFenceFirstSuiteFixture &) {
    suite_group_fence_order.clear();
    suite_group_fence_release.reset();
    suite_group_fence_order.emplace_back("first:start");
    co_await suite_group_fence_release.wait("suite fixture group boundary was crossed");
    suite_group_fence_order.emplace_back("first:resumed");
}

[[using gentest: test("fixture/group_fence_suite/01_second_release")]]
void fixture_suite_group_fence_second_release(GroupFenceSecondSuiteFixture &) {
    ASSERT_EQ(suite_group_fence_order.size(), std::size_t{1});
    EXPECT_EQ(suite_group_fence_order[0], "first:start");
    suite_group_fence_order.emplace_back("second:release");
    suite_group_fence_release.set();
}

[[using gentest: test("fixture/group_fence_suite/02_second_check")]]
void fixture_suite_group_fence_second_check(GroupFenceSecondSuiteFixture &) {
    ASSERT_EQ(suite_group_fence_order.size(), std::size_t{2});
    EXPECT_EQ(suite_group_fence_order[0], "first:start");
    EXPECT_EQ(suite_group_fence_order[1], "second:release");
}

[[using gentest: test("adopted_resume/worker_releases")]]
gentest::async_test<void> adopted_worker_releases() {
    adopted_resume_event.reset();

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    JoiningThread worker(std::thread([context = std::move(context), started = std::move(started)] {
        auto adoption = gentest::set_current_context(context);
        started->set_value();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        adopted_resume_event.set();
    }));

    ready.wait();
    co_await adopted_resume_event.wait("adopted worker should release the async test");
    EXPECT_TRUE(true);
}

[[using gentest: test("adopted_resume/worker_waits_for_resumed_ack")]]
gentest::async_test<void> adopted_worker_waits_for_resumed_ack() {
    gentest::async::manual_event release;
    auto                         context = gentest::get_current_context();
    auto                         started = std::make_shared<std::promise<void>>();
    auto                         ready   = started->get_future();
    std::promise<void>           resumed;
    auto                         resumed_ready = resumed.get_future();

    JoiningThread worker(std::thread(
        [context = std::move(context), started = std::move(started), &release, resumed_ready = std::move(resumed_ready)]() mutable {
            auto adoption = gentest::set_current_context(context);
            started->set_value();
            release.set();
            EXPECT_TRUE(resumed_ready.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
        }));

    ready.wait();
    co_await release.wait("adopted worker should wake scheduler before releasing context");
    resumed.set_value();
    EXPECT_TRUE(true);
}

[[using gentest: test("completion_source/order/00_waiter")]]
gentest::async_test<void> completion_source_first_terminal_waiter() {
    completion_order_source = std::make_shared<gentest::async::completion_source>();
    co_await completion_order_source->wait("waiting for first terminal state");
    EXPECT_TRUE(true);
}

[[using gentest: test("completion_source/order/01_driver")]]
gentest::async_test<void> completion_source_first_terminal_driver() {
    ASSERT_TRUE(static_cast<bool>(completion_order_source));
    completion_order_source->complete();
    completion_order_source->fail_unresumable("late failure must not replace completion");
    co_await gentest::async::yield();
    EXPECT_TRUE(true);
}

[[using gentest: test("completion_source/pre_failed_before_wait")]]
gentest::async_test<void> completion_source_pre_failed_before_wait() {
    gentest::async::completion_source source;
    source.fail_unresumable("pre-completed dependency cannot resume");
    co_await source.wait("pre-completed source should not suspend");
}

[[using gentest: test("blocked/explicit/00_waiter")]]
gentest::async_test<void> explicit_blocked_waiter() {
    explicit_blocked_source = std::make_shared<gentest::async::completion_source>();
    co_await explicit_blocked_source->wait("waiting for explicit blocked marker");
}

[[using gentest: test("blocked/explicit/01_driver")]]
gentest::async_test<void> explicit_blocked_driver() {
    ASSERT_TRUE(static_cast<bool>(explicit_blocked_source));
    explicit_blocked_source->fail_unresumable("external dependency cannot resume");
    co_return;
}

[[using gentest: test("blocked/after_failure")]]
gentest::async_test<void> blocked_after_failure_reports_failure() {
    EXPECT_TRUE(false, "failure-before-blocked-marker");
    gentest::async::completion_source source;
    source.fail_unresumable("dependency failed after assertion");
    co_await source.wait("pre-completed source should not mask assertion failure");
}

[[using gentest: test("blocked/empty_reason/00_waiter")]]
gentest::async_test<void> empty_reason_blocked_waiter() {
    empty_blocked_source = std::make_shared<gentest::async::completion_source>();
    co_await empty_blocked_source->wait("waiting for empty blocked marker");
}

[[using gentest: test("blocked/empty_reason/01_driver")]]
gentest::async_test<void> empty_reason_blocked_driver() {
    ASSERT_TRUE(static_cast<bool>(empty_blocked_source));
    empty_blocked_source->fail_unresumable();
    co_return;
}

[[using gentest: test("outcome/xfail_expect_fail_after_suspend")]]
gentest::async_test<void> outcome_xfail_expect_fail_after_suspend() {
    gentest::xfail("expected async expectation failure");
    co_await gentest::async::yield();
    EXPECT_TRUE(false);
}

[[using gentest: test("outcome/xfail_throw_after_suspend")]]
gentest::async_test<void> outcome_xfail_throw_after_suspend() {
    gentest::xfail("expected async exception");
    co_await gentest::async::yield();
    throw std::runtime_error("expected async throw");
}

[[using gentest: test("outcome/xfail_xpass_after_suspend")]]
gentest::async_test<void> outcome_xfail_xpass_after_suspend() {
    gentest::xfail("expected async failure that does not happen");
    co_await gentest::async::yield();
    EXPECT_TRUE(true);
}

[[using gentest: test("outcome/skip_overrides_xfail_after_suspend")]]
gentest::async_test<void> outcome_skip_overrides_xfail_after_suspend() {
    gentest::xfail("skip should win over async xfail");
    co_await gentest::async::yield();
    gentest::skip("async runtime skip overrides xfail");
}

[[using gentest: test("outcome/runtime_skip_after_suspend")]]
gentest::async_test<void> outcome_runtime_skip_after_suspend() {
    co_await gentest::async::yield();
    gentest::skip("async runtime skip");
}

[[using gentest: test("outcome/skip_after_failure_is_fail_after_suspend")]]
gentest::async_test<void> outcome_skip_after_failure_is_fail_after_suspend() {
    co_await gentest::async::yield();
    EXPECT_TRUE(false);
    gentest::skip("async skip after failure must not mask the failure");
}

[[using gentest: test("waiter_lifetime/process_exit_unresumable")]]
gentest::async_test<void> waiter_lifetime_process_exit_unresumable() {
    process_exit_event.reset();
    co_await process_exit_event.wait("process-exit signal arrives after scheduler destruction");
}

[[using gentest: test("blocked/never")]]
gentest::async_test<void> blocked_never() {
    gentest::async::manual_event never;
    co_await never.wait("never ready");
}

} // namespace async
