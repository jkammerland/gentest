#include "cases.hpp"

namespace async {

gentest::async_test<void> wait_for_child_pass() {
    co_await gentest::async::yield();
    co_return;
}

} // namespace async

namespace async {

gentest::async_test<int> wait_for_child_value() {
    co_await gentest::async::yield();
    co_return 42;
}

} // namespace async

namespace async {

gentest::async_test<void> wait_for_child_throws() {
    co_await gentest::async::yield();
    throw std::runtime_error("wait_for async_test exception marker");
}

} // namespace async

namespace async {

gentest::async_test<void> wait_for_child_never(gentest::async::manual_event &event) { co_await event.wait("child release"); }

} // namespace async

namespace async {

gentest::async_test<void> wait_for_nested_child_never(gentest::async::manual_event &event) { co_await wait_for_child_never(event); }

} // namespace async

namespace async {

gentest::async_test<int> concurrent_lvalue_value_child() {
    concurrent_lvalue_value_child_started.set("value child started");
    co_await concurrent_lvalue_value_child_release.wait("release concurrent lvalue value child");
    co_return 42;
}

} // namespace async

namespace async {

gentest::async_test<void> concurrent_lvalue_void_child() {
    concurrent_lvalue_void_child_started.set("void child started");
    co_await concurrent_lvalue_void_child_release.wait("release concurrent lvalue void child");
}

} // namespace async

namespace async {

gentest::async_test<void> server() {
    reset_batch_if_previous_round_completed();
    order.clear();
    co_await gentest::async::yield();
    order.emplace_back("server");
    server_ready.set("batch server ready");
    co_await client_done.wait("batch client done");
    ASSERT_EQ(order.size(), std::size_t{2});
    EXPECT_EQ(order[0], "server");
    EXPECT_EQ(order[1], "client");
}

} // namespace async

namespace async {

gentest::async_test<void> client() {
    reset_batch_if_previous_round_completed();
    co_await server_ready.wait("batch server ready");
    order.emplace_back("client");
    client_done.set("batch client done");
    ASSERT_EQ(order.size(), std::size_t{2});
    EXPECT_EQ(order[0], "server");
    EXPECT_EQ(order[1], "client");
}

} // namespace async

namespace async {

gentest::async_test<void> live_demo_suspended() {
    live_demo_order.clear();
    live_demo_resume.reset_all();
    live_demo_order.emplace_back("suspended:start");

    co_await live_demo_resume.wait("live demo driver completed");

    live_demo_order.emplace_back("suspended:resumed");
    ASSERT_EQ(live_demo_order.size(), std::size_t{3});
    EXPECT_EQ(live_demo_order[0], "suspended:start");
    EXPECT_EQ(live_demo_order[1], "driver:complete");
    EXPECT_EQ(live_demo_order[2], "suspended:resumed");
}

} // namespace async

namespace async {

gentest::async_test<void> live_demo_driver() {
    for (int i = 0; i < kLiveDemoRounds; ++i) {
        co_await gentest::async::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
    }

    live_demo_order.emplace_back("driver:complete");
    live_demo_resume.set("live demo driver completed");
}

} // namespace async

namespace async {

gentest::async_test<void> fail_fast_async_fail() {
    co_await gentest::async::yield();
    EXPECT_TRUE(false);
}

} // namespace async

namespace async {

void fail_fast_sync_should_not_run() { gentest::fail("fail-fast allowed a later sync case to run"); }

} // namespace async

namespace async {

gentest::async_test<void> fail_fast_snapshot_async_fail_after_release() {
    fail_fast_snapshot_release.reset_all();
    co_await fail_fast_snapshot_release.wait("fail-fast snapshot release");
    EXPECT_TRUE(false);
}

} // namespace async

namespace async {

gentest::async_test<void> fail_fast_snapshot_async_should_not_run_after_failure() {
    co_await fail_fast_snapshot_release.wait("fail-fast snapshot release");
    gentest::fail("fail-fast allowed a later ready async case to run");
}

} // namespace async

namespace async {

void fail_fast_snapshot_release_waiters() { fail_fast_snapshot_release.set("fail-fast snapshot release"); }

} // namespace async

namespace async {

gentest::async_test<void> fail_fast_self_adopted_async_fails_with_adopted_worker() {
    fail_fast_self_adopted_worker_started.store(false, std::memory_order_release);
    fail_fast_self_adopted_worker_done.store(false, std::memory_order_release);

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        fail_fast_self_adopted_worker_started.store(true, std::memory_order_release);
        started->set_value();
        while (context && !context.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        gentest::log("self-adopted worker observed fail-fast cancellation");
        fail_fast_self_adopted_worker_done.store(true, std::memory_order_release);
    }).detach();

    ready.wait();
    co_await gentest::async::yield();
    EXPECT_TRUE(false, "fail-fast async failure should cancel before waiting for adopted work");
}

} // namespace async

namespace async {

void fail_fast_self_adopted_sync_should_not_run() { gentest::fail("fail-fast allowed a later self-adopted sync case to run"); }

} // namespace async

namespace async {

gentest::async_test<void> fail_fast_done_adopted_late_done_with_late_worker_log() {
    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        started->set_value();
        while (context && !context.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        gentest::log("adopted worker logged after completed async cancellation");
    }).detach();

    ready.wait();
    co_return;
}

} // namespace async

namespace async {

void fail_fast_done_adopted_late_sync_failure() {
    EXPECT_TRUE(false, "trigger fail-fast while completed async case still has an adopted worker lease");
}

} // namespace async

namespace async {

void fail_fast_done_adopted_late_sync_should_not_run() {
    gentest::fail("fail-fast allowed a later sync case after late adopted worker log");
}

} // namespace async

namespace async {

gentest::async_test<void> fail_fast_cancel_adopted_context_pending_worker_logs_after_cancel() {
    fail_fast_cancel_adopted_context_resume.reset_all();
    fail_fast_cancel_adopted_context_worker_started.store(false, std::memory_order_release);
    fail_fast_cancel_adopted_context_worker_done.store(false, std::memory_order_release);

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        fail_fast_cancel_adopted_context_worker_started.store(true, std::memory_order_release);
        started->set_value();
        while (context && !context.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        gentest::log("adopted worker logged after fail-fast cancellation");
        fail_fast_cancel_adopted_context_worker_done.store(true, std::memory_order_release);
    }).detach();

    ready.wait();
    co_await fail_fast_cancel_adopted_context_resume.wait("fail-fast adopted context release");
    gentest::fail("fail-fast cancellation resumed pending adopted context case");
}

} // namespace async

namespace async {

void fail_fast_cancel_adopted_context_sync_failure() {
    EXPECT_TRUE(false, "fail-fast sync failure should not make adopted context operations fatal");
}

} // namespace async

namespace async {

gentest::async_test<void> fail_fast_cancel_released_context_pending_worker_reuses_released_context() {
    fail_fast_cancel_adopted_context_resume.reset_all();
    fail_fast_cancel_released_context_worker_started.store(false, std::memory_order_release);
    fail_fast_cancel_released_context_worker_done.store(false, std::memory_order_release);

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        {
            auto lease = gentest::set_current_context(context);
            fail_fast_cancel_released_context_worker_started.store(true, std::memory_order_release);
            started->set_value();
            while (context && !context.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            gentest::log("adopted worker logged during fail-fast cancellation");
        }

        auto stale_lease = gentest::set_current_context(context);
        gentest::log("stale adopted worker logged after releasing fail-fast cancellation context");
        fail_fast_cancel_released_context_worker_done.store(true, std::memory_order_release);
    }).detach();

    ready.wait();
    co_await fail_fast_cancel_adopted_context_resume.wait("fail-fast adopted context release");
    gentest::fail("fail-fast cancellation resumed released context case");
}

} // namespace async

namespace async {

void fail_fast_cancel_released_context_sync_failure() {
    EXPECT_TRUE(false, "fail-fast sync failure should close adopted contexts after release");
}

} // namespace async

namespace async {

gentest::async_test<void> fail_fast_cancel_local_fixture_pending(FailFastCancelLocalFixture &) {
    fail_fast_cancel_local_fixture_resume.reset_all();
    co_await fail_fast_cancel_local_fixture_resume.wait("fail-fast local fixture release");
    gentest::fail("fail-fast cancellation resumed pending local fixture case");
}

} // namespace async

namespace async {

void fail_fast_cancel_local_fixture_sync_failure() {
    EXPECT_TRUE(false, "fail-fast sync failure should cancel pending async local fixture with teardown");
}

} // namespace async

namespace async {

gentest::async_test<void> fail_fast_cancel_adopted_local_fixture_pending(FailFastCancelAdoptedLocalFixture &) {
    fail_fast_cancel_adopted_local_fixture_resume.reset_all();
    fail_fast_cancel_adopted_local_fixture_started.store(false, std::memory_order_release);
    fail_fast_cancel_adopted_local_fixture_worker_done.store(false, std::memory_order_release);
    fail_fast_cancel_adopted_local_fixture_torn_down.store(false, std::memory_order_release);
    fail_fast_cancel_adopted_local_fixture_frame_destroyed.store(false, std::memory_order_release);
    FailFastCancelAdoptedLocalFrameGuard frame_guard;

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        fail_fast_cancel_adopted_local_fixture_started.store(true, std::memory_order_release);
        started->set_value();
        while (context && !context.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        gentest::log("adopted worker observed fail-fast local fixture cancellation");
        fail_fast_cancel_adopted_local_fixture_worker_done.store(true, std::memory_order_release);
    }).detach();

    ready.wait();
    co_await fail_fast_cancel_adopted_local_fixture_resume.wait("fail-fast adopted local fixture release");
    gentest::fail("fail-fast cancellation resumed adopted local fixture case");
}

} // namespace async

namespace async {

void fail_fast_cancel_adopted_local_fixture_sync_failure() {
    EXPECT_TRUE(false, "fail-fast sync failure should trigger adopted async local fixture teardown");
}

} // namespace async

namespace async {

gentest::async_test<void> fail_fast_cancel_slow_adopted_pending_needs_resume() {
    fail_fast_cancel_slow_adopted_resume.reset_all();
    fail_fast_cancel_slow_adopted_worker_started.store(false, std::memory_order_release);
    fail_fast_cancel_slow_adopted_worker_done.store(false, std::memory_order_release);
    fail_fast_cancel_slow_adopted_frame_destroyed.store(false, std::memory_order_release);
    FailFastCancelSlowAdoptedFrameGuard frame_guard;

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        fail_fast_cancel_slow_adopted_worker_started.store(true, std::memory_order_release);
        started->set_value();
        while (context && !context.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        gentest::log("slow adopted worker released after cancellation");
        fail_fast_cancel_slow_adopted_worker_done.store(true, std::memory_order_release);
    }).detach();

    ready.wait();
    co_await fail_fast_cancel_slow_adopted_resume.wait("fail-fast slow adopted release");
    gentest::fail("fail-fast cancellation resumed slow adopted case");
}

} // namespace async

namespace async {

void fail_fast_cancel_slow_adopted_sync_failure() { EXPECT_TRUE(false, "fail-fast sync failure should wait for slow adopted release"); }

} // namespace async

namespace async {

gentest::async_test<void> fail_fast_cancel_adopted_skip_pending_worker_logs_after_cancel() {
    fail_fast_cancel_adopted_skip_resume.reset_all();
    fail_fast_cancel_adopted_skip_worker_started.store(false, std::memory_order_release);
    fail_fast_cancel_adopted_skip_worker_done.store(false, std::memory_order_release);

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        fail_fast_cancel_adopted_skip_worker_started.store(true, std::memory_order_release);
        started->set_value();
        while (context && !context.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        gentest::log("adopted worker logged after fail-fast cancellation");
        fail_fast_cancel_adopted_skip_worker_done.store(true, std::memory_order_release);
    }).detach();

    ready.wait();
    co_await fail_fast_cancel_adopted_skip_resume.wait("fail-fast adopted skip release");
    gentest::fail("fail-fast cancellation resumed adopted skip case");
}

} // namespace async

namespace async {

void fail_fast_cancel_adopted_skip_sync_failure() { EXPECT_TRUE(false, "fail-fast sync failure should not make adopted skip fatal"); }

} // namespace async

namespace async {

gentest::async_test<void> fail_fast_final_drain_async_fail_after_adopted_release() {
    fail_fast_final_drain_fail_release.reset_all();
    fail_fast_final_drain_late_release.reset_all();
    fail_fast_final_drain_waiters.store(0, std::memory_order_release);

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    JoiningThread worker(std::thread([context = std::move(context), started = std::move(started)]() mutable {
        auto lease = gentest::set_current_context(context);
        started->set_value();
        while (fail_fast_final_drain_waiters.load(std::memory_order_acquire) < 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        fail_fast_final_drain_fail_release.set("fail-fast final drain failing release");
        fail_fast_final_drain_late_release.set("fail-fast final drain late release");
    }));
    ready.wait();

    fail_fast_final_drain_waiters.fetch_add(1, std::memory_order_acq_rel);
    co_await fail_fast_final_drain_fail_release.wait("fail-fast final drain failing release");
    EXPECT_TRUE(false);
}

} // namespace async

namespace async {

gentest::async_test<void> fail_fast_final_drain_async_should_not_run_after_failure() {
    fail_fast_final_drain_waiters.fetch_add(1, std::memory_order_acq_rel);
    co_await fail_fast_final_drain_late_release.wait("fail-fast final drain late release");
    gentest::fail("fail-fast final drain allowed a later ready async case to run");
}

} // namespace async

namespace async {

gentest::async_test<void> timer_sleep_for_waits() {
    timer_fairness_order.clear();
    co_await gentest::async::sleep_for(std::chrono::milliseconds(10));
    ASSERT_EQ(timer_fairness_order.size(), std::size_t{1});
    EXPECT_EQ(timer_fairness_order[0], "yield");
    timer_fairness_order.emplace_back("timer");
}

} // namespace async

namespace async {

gentest::async_test<void> timer_yield_runs_while_sleep_pending() {
    co_await gentest::async::yield();
    timer_fairness_order.emplace_back("yield");
}

} // namespace async

namespace async {

gentest::async_test<void> timer_sleep_until_waits() {
    co_await gentest::async::sleep_until(std::chrono::steady_clock::now() + std::chrono::milliseconds(1));
    EXPECT_TRUE(true);
}

} // namespace async

namespace async {

gentest::async_test<void> timer_stress_ready_wins_expired_timeout() {
    gentest::async::event<int> event;

    for (int i = 0; i != kTimerStressIterations; ++i) {
        const auto key = "ready." + std::to_string(i);
        event.set(key, i);

        auto result = co_await gentest::async::wait_for(event.wait(key), std::chrono::milliseconds(-1));

        ASSERT_TRUE(result.ready());
        EXPECT_EQ(result.value(), i);
    }
}

} // namespace async

namespace async {

gentest::async_test<void> timer_stress_timeout_cancels_late_event_waiter() {
    gentest::async::event<int> event;

    for (int i = 0; i != kTimerStressIterations; ++i) {
        const auto key = "late." + std::to_string(i);

        auto result = co_await gentest::async::wait_for(event.wait(key), std::chrono::milliseconds(1));

        ASSERT_TRUE(result.timed_out());
        event.set(key, i);
        co_await gentest::async::yield();
        EXPECT_TRUE(result.timed_out());
    }
}

} // namespace async

namespace async {

gentest::async_test<void> timer_stress_timeout_cancels_inner_sleep_timer() {
    for (int i = 0; i != kTimerStressIterations; ++i) {
        auto result =
            co_await gentest::async::wait_for(gentest::async::sleep_for(std::chrono::milliseconds(25)), std::chrono::milliseconds(1));

        EXPECT_TRUE(result.timed_out());
    }

    auto flush =
        co_await gentest::async::wait_for(gentest::async::sleep_for(std::chrono::milliseconds(30)), std::chrono::milliseconds(250));
    EXPECT_TRUE(flush.ready());
}

} // namespace async

namespace async {

auto timer_stress_simultaneous_sleeper() -> gentest::async_test<void> {
    co_await timer_stress_start.wait("timer stress start");

    const int started = timer_stress_started.fetch_add(1, std::memory_order_acq_rel) + 1;
    EXPECT_TRUE(started <= kTimerStressSleepers);
    if (started == kTimerStressSleepers) {
        timer_stress_all_started.set("timer stress all started");
    }

    const auto deadline = std::chrono::steady_clock::time_point{
        std::chrono::steady_clock::duration{timer_stress_deadline_ticks.load(std::memory_order_acquire)}};
    co_await gentest::async::sleep_until(deadline);

    const int completed = timer_stress_completed.fetch_add(1, std::memory_order_acq_rel) + 1;
    EXPECT_TRUE(completed <= kTimerStressSleepers);
    if (completed == kTimerStressSleepers) {
        timer_stress_all_done.set("timer stress all done");
    }
}

} // namespace async

namespace async {

gentest::async_test<void> timer_stress_simultaneous_driver() {
    timer_stress_start.reset_all();
    timer_stress_all_started.reset_all();
    timer_stress_all_done.reset_all();
    timer_stress_started.store(0, std::memory_order_release);
    timer_stress_completed.store(0, std::memory_order_release);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(25);
    timer_stress_deadline_ticks.store(deadline.time_since_epoch().count(), std::memory_order_release);

    timer_stress_start.set("timer stress start");

    auto started =
        co_await gentest::async::wait_for(timer_stress_all_started.wait("timer stress all started"), std::chrono::milliseconds(250));
    ASSERT_TRUE(started.ready());
    EXPECT_EQ(timer_stress_started.load(std::memory_order_acquire), kTimerStressSleepers);

    auto completed = co_await gentest::async::wait_for(timer_stress_all_done.wait("timer stress all done"), std::chrono::milliseconds(250));
    ASSERT_TRUE(completed.ready());
    EXPECT_EQ(timer_stress_completed.load(std::memory_order_acquire), kTimerStressSleepers);
}

} // namespace async

namespace async {

gentest::async_test<void> timer_stress_simultaneous_sleeper_a() { co_await timer_stress_simultaneous_sleeper(); }

} // namespace async

namespace async {

gentest::async_test<void> timer_stress_simultaneous_sleeper_b() { co_await timer_stress_simultaneous_sleeper(); }

} // namespace async

namespace async {

gentest::async_test<void> timer_stress_simultaneous_sleeper_c() { co_await timer_stress_simultaneous_sleeper(); }

} // namespace async

namespace async {

gentest::async_test<void> timer_stress_simultaneous_sleeper_d() { co_await timer_stress_simultaneous_sleeper(); }

} // namespace async

namespace async {

gentest::async_test<void> wait_for_event_ready_before_timeout() {
    gentest::async::event<int> event;
    event.set("ready", 7);

    auto result = co_await gentest::async::wait_for(event.wait("ready"), std::chrono::milliseconds(50));

    EXPECT_TRUE(result.ready());
    EXPECT_FALSE(result.timed_out());
    EXPECT_TRUE(result.status() == gentest::async::wait_status::ready);
    EXPECT_EQ(result.value(), 7);
    result.value() = 8;
    int &payload   = co_await event.wait("ready");
    EXPECT_EQ(payload, 8);
}

} // namespace async

namespace async {

gentest::async_test<void> wait_for_event_timeout_late_signal() {
    gentest::async::event<int> event;

    auto result = co_await gentest::async::wait_for(event.wait("late"), std::chrono::milliseconds(1));

    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_TRUE(result.timed_out());
    EXPECT_TRUE(result.status() == gentest::async::wait_status::timeout);
    event.set("late", 9);
    co_await gentest::async::yield();
    EXPECT_TRUE(result.timed_out());
}

} // namespace async

namespace async {

gentest::async_test<void> wait_for_future_void_ready() {
    gentest::async::promise<void> promise;
    auto                          future = promise.get_future();
    promise.set_value();

    auto result = co_await gentest::async::wait_for(future.wait("future void ready"), std::chrono::milliseconds(50));

    EXPECT_TRUE(result.ready());
}

} // namespace async

namespace async {

gentest::async_test<void> wait_for_future_value_timeout() {
    gentest::async::promise<int> promise;
    auto                         future = promise.get_future();

    auto result = co_await gentest::async::wait_for(future.wait("future value timeout"), std::chrono::milliseconds(1));

    EXPECT_TRUE(result.timed_out());
    promise.set_value(11);
    co_await gentest::async::yield();
    EXPECT_TRUE(result.timed_out());
}

} // namespace async

namespace async {

gentest::async_test<void> wait_for_future_move_only_ready() {
    gentest::async::promise<std::unique_ptr<int>> promise;
    auto                                          future = promise.get_future();
    promise.set_value(std::make_unique<int>(17));

    auto result = co_await gentest::async::wait_for(future.wait("future move-only ready"), std::chrono::milliseconds(50));

    ASSERT_TRUE(result.ready());
    auto payload = std::move(result).value();
    ASSERT_TRUE(static_cast<bool>(payload));
    EXPECT_EQ(*payload, 17);
}

} // namespace async

namespace async {

gentest::async_test<void> wait_for_async_test_pass() {
    auto result = co_await gentest::async::wait_for(wait_for_child_pass(), std::chrono::milliseconds(50));
    EXPECT_TRUE(result.ready());
}

} // namespace async

namespace async {

gentest::async_test<void> wait_for_async_test_value() {
    auto result = co_await gentest::async::wait_for(wait_for_child_value(), std::chrono::milliseconds(50));
    ASSERT_TRUE(result.ready());
    EXPECT_EQ(result.value(), 42);
}

} // namespace async

namespace async {

gentest::async_test<void> wait_for_async_test_exception() {
    try {
        (void)(co_await gentest::async::wait_for(wait_for_child_throws(), std::chrono::milliseconds(50)));
        gentest::fail("wait_for did not propagate async_test exception");
    } catch (const std::runtime_error &ex) {
        EXPECT_TRUE(std::string_view(ex.what()).find("wait_for async_test exception marker") != std::string_view::npos);
    }
}

} // namespace async

namespace async {

gentest::async_test<void> wait_for_async_test_timeout_cancels_stale_waiter() {
    gentest::async::manual_event event;

    auto result = co_await gentest::async::wait_for(wait_for_child_never(event), std::chrono::milliseconds(1));

    EXPECT_TRUE(result.timed_out());
    event.set("child release");
    co_await gentest::async::yield();
    EXPECT_TRUE(result.timed_out());
}

} // namespace async

namespace async {

gentest::async_test<void> wait_for_async_test_timeout_cancels_nested_stale_waiter() {
    gentest::async::manual_event event;

    auto result = co_await gentest::async::wait_for(wait_for_nested_child_never(event), std::chrono::milliseconds(1));

    EXPECT_TRUE(result.timed_out());
    event.set("child release");
    co_await gentest::async::yield();
    EXPECT_TRUE(result.timed_out());
}

} // namespace async

namespace async {

gentest::async_test<void> lvalue_await_first_waiter() {
    concurrent_lvalue_value_task = concurrent_lvalue_value_child();

    const int value = co_await concurrent_lvalue_value_task;

    EXPECT_EQ(value, 42);
    concurrent_lvalue_void_task = concurrent_lvalue_void_child();
    co_await concurrent_lvalue_void_task;
    concurrent_lvalue_first_waiter_done.set("first waiter completed");
}

} // namespace async

namespace async {

gentest::async_test<void> lvalue_await_concurrent_waiter() {
    co_await concurrent_lvalue_value_child_started.wait("value child started");
    concurrent_lvalue_value_child_release.set("release concurrent lvalue value child");

    try {
        (void)(co_await concurrent_lvalue_value_task);
        gentest::fail("concurrent lvalue co_await of async_test<T> unexpectedly succeeded");
    } catch (const std::logic_error &ex) {
        EXPECT_EQ(std::string_view(ex.what()),
                  std::string_view("gentest::async_test already has an active waiter (concurrent co_await is not supported)"));
    }

    co_await concurrent_lvalue_void_child_started.wait("void child started");
    concurrent_lvalue_void_child_release.set("release concurrent lvalue void child");

    co_await concurrent_lvalue_void_task;
    gentest::fail("concurrent lvalue co_await of async_test<void> unexpectedly succeeded");
}

} // namespace async

namespace async {

gentest::async_test<void> lvalue_await_completed_task() {
    co_await concurrent_lvalue_first_waiter_done.wait("first waiter completed");

    EXPECT_EQ(co_await concurrent_lvalue_value_task, 42);
    co_await concurrent_lvalue_void_task;
}

} // namespace async

namespace async {

gentest::async_test<int> value_discard() {
    co_await gentest::async::yield();
    EXPECT_TRUE(true);
    co_return 7;
}

} // namespace async

namespace async {

void case_api_sync_fn_async_guard() {
    auto cases = gentest::registered_cases();
    auto it    = std::ranges::find_if(cases, [](const gentest::Case &test_case) { return test_case.name == "async/value/discard"; });
    ASSERT_TRUE(it != cases.end());
    ASSERT_TRUE(it->is_async);
    ASSERT_TRUE(it->async_fn != nullptr);
    it->fn(nullptr);
}

} // namespace async

namespace async {

gentest::async_test<void> local_async_fixture(LocalAsyncFixture &fixture) {
    EXPECT_EQ(fixture.value, 42);
    co_return;
}

} // namespace async

namespace async {

gentest::async_test<void> mixed_fixtures(LocalSyncFixture &sync_fixture, LocalAsyncFixture &async_fixture) {
    EXPECT_EQ(sync_fixture.value, 7);
    EXPECT_EQ(async_fixture.value, 42);
    co_return;
}

} // namespace async

namespace async {

gentest::async_test<void> parameterized_value(int value) {
    co_await gentest::async::yield();
    EXPECT_TRUE(value >= 1 && value <= 3);
}

} // namespace async

namespace async {

gentest::async_test<void> local_async_lifecycle_use(LocalAsyncLifecycleFixture &fixture) {
    EXPECT_EQ(fixture.value, 123);
    EXPECT_TRUE(fixture.setup);
    EXPECT_FALSE(fixture.torn_down);
    co_return;
}

} // namespace async

namespace async {

void shared_async_adopted_setup(BlockingSchedulerAdoptedFixture &fixture) { EXPECT_TRUE(fixture.setup); }

} // namespace async

namespace async {

gentest::async_test<void> local_async_teardown_preserves_primary_failure(LocalAsyncThrowingTeardownFixture &) {
    co_await gentest::async::yield();
    throw std::runtime_error("async-body-primary-marker");
}

} // namespace async

namespace async {

gentest::async_test<void> local_async_unresumable_reports_teardown(LocalAsyncFailingUnresumableTeardownFixture &) {
    local_unresumable_teardown_never.reset_all();
    co_await local_unresumable_teardown_never.wait("local async fixture teardown should report after unresumable body");
}

} // namespace async

namespace async {

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
    fixture.release_async.reset_all();
    fixture.events.clear();
    EXPECT_EQ(fixture.value, 10);
    fixture.saw_async = true;
    fixture.events.emplace_back("async:start");

    co_await fixture.release_async.wait("shared suite fixture released async case");

    EXPECT_EQ(fixture.value, 20);
    fixture.events.emplace_back("async:done");
}

} // namespace async

namespace async {

void shared_suite_sync_release(SharedSuiteAsyncFixture &fixture) {
    EXPECT_EQ(&fixture, SharedSuiteAsyncFixture::first);
    EXPECT_EQ(SharedSuiteAsyncFixture::setups, 1);
    EXPECT_EQ(fixture.value, 10);
    fixture.saw_sync = true;
    fixture.value    = 20;
    fixture.events.emplace_back("sync:release");
    fixture.release_async.set("shared suite fixture released async case");
}

} // namespace async

namespace async {

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

} // namespace async

namespace async {

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
    fixture.release_async.reset_all();
    fixture.events.clear();
    EXPECT_EQ(fixture.value, 100);
    fixture.saw_seed = true;
    fixture.events.emplace_back("sync:seed");
}

} // namespace async

namespace async {

gentest::async_test<void> shared_global_async_wait(SharedGlobalAsyncHandle fixture) {
    ASSERT_TRUE(static_cast<bool>(fixture));
    EXPECT_EQ(fixture.get(), SharedGlobalAsyncFixture::first);
    EXPECT_EQ(SharedGlobalAsyncFixture::setups, 1);
    EXPECT_TRUE(fixture->saw_seed);
    EXPECT_EQ(fixture->value, 100);
    fixture->saw_async = true;
    fixture->events.emplace_back("async:start");

    co_await fixture->release_async.wait("shared global fixture released async case");

    EXPECT_EQ(fixture->value, 200);
    fixture->events.emplace_back("async:done");
}

} // namespace async

namespace async {

void shared_global_sync_release(SharedGlobalAsyncRawAlias fixture) {
    ASSERT_TRUE(fixture != nullptr);
    EXPECT_EQ(fixture, SharedGlobalAsyncFixture::first);
    EXPECT_TRUE(fixture->saw_seed);
    EXPECT_TRUE(fixture->saw_async);
    fixture->saw_sync = true;
    fixture->value    = 200;
    fixture->events.emplace_back("sync:release");
    fixture->release_async.set("shared global fixture released async case");
}

} // namespace async

namespace async {

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

} // namespace async

namespace async {

gentest::async_test<void> shared_suite_unresumable_never(SharedSuiteUnresumableFixture &fixture) {
    EXPECT_EQ(fixture.value, 300);
    fixture.saw_async = true;
    co_await fixture.never.wait("suite fixture resume handle was not signalled");
}

} // namespace async

namespace async {

void shared_suite_unresumable_sync_after(SharedSuiteUnresumableFixture &fixture) {
    EXPECT_TRUE(fixture.saw_async);
    fixture.saw_sync = true;
    fixture.value    = 301;
}

} // namespace async

namespace async {

gentest::async_test<void> shared_global_unresumable_never(SharedGlobalUnresumableFixture &fixture) {
    EXPECT_EQ(fixture.value, 400);
    fixture.saw_async = true;
    co_await fixture.never.wait("global fixture resume handle was not signalled");
}

} // namespace async

namespace async {

void shared_global_unresumable_sync_after(SharedGlobalUnresumableFixture &fixture) {
    EXPECT_TRUE(fixture.saw_async);
    fixture.saw_sync = true;
    fixture.value    = 401;
}

} // namespace async

namespace async {

gentest::async_test<void> fixture_group_fence_async_wait() {
    group_fence_order.clear();
    group_fence_release.reset_all();
    group_fence_order.emplace_back("async:start");
    co_await group_fence_release.wait("fixture group boundary was crossed");
    group_fence_order.emplace_back("async:resumed");
}

} // namespace async

namespace async {

void fixture_group_fence_suite_release(LocalSyncFixture &local_fixture, GroupFenceSuiteFixture &) {
    EXPECT_EQ(local_fixture.value, 7);
    ASSERT_EQ(group_fence_order.size(), std::size_t{1});
    EXPECT_EQ(group_fence_order[0], "async:start");
    group_fence_order.emplace_back("suite:release");
    group_fence_release.set("fixture group boundary was crossed");
}

} // namespace async

namespace async {

void fixture_group_fence_suite_check(GroupFenceSuiteFixture &) {
    ASSERT_EQ(group_fence_order.size(), std::size_t{2});
    EXPECT_EQ(group_fence_order[0], "async:start");
    EXPECT_EQ(group_fence_order[1], "suite:release");
}

} // namespace async

namespace async {

gentest::async_test<void> fixture_suite_group_fence_async_wait(GroupFenceFirstSuiteFixture &) {
    suite_group_fence_order.clear();
    suite_group_fence_release.reset_all();
    suite_group_fence_order.emplace_back("first:start");
    co_await suite_group_fence_release.wait("suite fixture group boundary was crossed");
    suite_group_fence_order.emplace_back("first:resumed");
}

} // namespace async

namespace async {

void fixture_suite_group_fence_second_release(GroupFenceSecondSuiteFixture &) {
    ASSERT_EQ(suite_group_fence_order.size(), std::size_t{1});
    EXPECT_EQ(suite_group_fence_order[0], "first:start");
    suite_group_fence_order.emplace_back("second:release");
    suite_group_fence_release.set("suite fixture group boundary was crossed");
}

} // namespace async

namespace async {

void fixture_suite_group_fence_second_check(GroupFenceSecondSuiteFixture &) {
    ASSERT_EQ(suite_group_fence_order.size(), std::size_t{2});
    EXPECT_EQ(suite_group_fence_order[0], "first:start");
    EXPECT_EQ(suite_group_fence_order[1], "second:release");
}

} // namespace async

namespace async {

gentest::async_test<void> adopted_worker_releases() {
    adopted_resume_event.reset_all();

    auto context = gentest::get_current_context();
    auto started = std::make_shared<std::promise<void>>();
    auto ready   = started->get_future();

    JoiningThread worker(std::thread([context = std::move(context), started = std::move(started)] {
        auto lease = gentest::set_current_context(context);
        started->set_value();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        adopted_resume_event.set("adopted worker released async case");
    }));

    ready.wait();
    co_await adopted_resume_event.wait("adopted worker released async case");
    EXPECT_TRUE(true);
}

} // namespace async

namespace async {

gentest::async_test<void> adopted_worker_waits_for_resumed_ack() {
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
            release.set("adopted worker acknowledged resume");
            worker_saw_resume.store(resumed_ready.wait_for(std::chrono::seconds(1)) == std::future_status::ready,
                                    std::memory_order_release);
        }));

        ready.wait();
        co_await release.wait("adopted worker acknowledged resume");
        resumed.set_value();
    }
    EXPECT_TRUE(worker_saw_resume.load(std::memory_order_acquire));
}

} // namespace async

namespace async {

gentest::async_test<void> promise_first_terminal_waiter() {
    promise_order_source = std::make_shared<gentest::async::promise<void>>();
    auto future          = promise_order_source->get_future();
    co_await future.wait("waiting for first terminal state");
    EXPECT_TRUE(true);
}

} // namespace async

namespace async {

gentest::async_test<void> promise_first_terminal_driver() {
    ASSERT_TRUE(static_cast<bool>(promise_order_source));
    promise_order_source->set_value();
    EXPECT_FALSE(promise_order_source->try_set_blocked("late failure must not replace completion"));
    co_await gentest::async::yield();
    EXPECT_TRUE(true);
}

} // namespace async

namespace async {

gentest::async_test<void> promise_pre_blocked_before_wait() {
    gentest::async::promise<void> source;
    auto                          future = source.get_future();
    source.set_blocked("pre-completed dependency cannot resume");
    co_await future.wait("pre-completed promise should not suspend");
}

} // namespace async

namespace async {

gentest::async_test<void> explicit_blocked_waiter() {
    explicit_blocked_source = std::make_shared<gentest::async::promise<void>>();
    auto future             = explicit_blocked_source->get_future();
    co_await future.wait("waiting for explicit blocked marker");
}

} // namespace async

namespace async {

gentest::async_test<void> explicit_blocked_driver() {
    ASSERT_TRUE(static_cast<bool>(explicit_blocked_source));
    explicit_blocked_source->set_blocked("external dependency cannot resume");
    co_return;
}

} // namespace async

namespace async {

gentest::async_test<void> blocked_after_failure_reports_failure() {
    EXPECT_TRUE(false, "failure-before-blocked-marker");
    gentest::async::promise<void> source;
    auto                          future = source.get_future();
    source.set_blocked("dependency failed after assertion");
    co_await future.wait("pre-completed promise should not mask assertion failure");
}

} // namespace async

namespace async {

gentest::async_test<void> empty_reason_blocked_waiter() {
    empty_blocked_source = std::make_shared<gentest::async::promise<void>>();
    auto future          = empty_blocked_source->get_future();
    co_await future.wait("waiting for empty blocked marker");
}

} // namespace async

namespace async {

gentest::async_test<void> empty_reason_blocked_driver() {
    ASSERT_TRUE(static_cast<bool>(empty_blocked_source));
    empty_blocked_source->set_blocked();
    co_return;
}

} // namespace async

namespace async {

gentest::async_test<void> outcome_xfail_expect_fail_after_suspend() {
    gentest::xfail("expected async expectation failure");
    co_await gentest::async::yield();
    EXPECT_TRUE(false);
}

} // namespace async

namespace async {

gentest::async_test<void> outcome_xfail_throw_after_suspend() {
    gentest::xfail("expected async exception");
    co_await gentest::async::yield();
    throw std::runtime_error("expected async throw");
}

} // namespace async

namespace async {

gentest::async_test<void> outcome_xfail_xpass_after_suspend() {
    gentest::xfail("expected async failure that does not happen");
    co_await gentest::async::yield();
    EXPECT_TRUE(true);
}

} // namespace async

namespace async {

gentest::async_test<void> outcome_skip_overrides_xfail_after_suspend() {
    gentest::xfail("skip should win over async xfail");
    co_await gentest::async::yield();
    gentest::skip("async runtime skip overrides xfail");
}

} // namespace async

namespace async {

gentest::async_test<void> outcome_runtime_skip_after_suspend() {
    co_await gentest::async::yield();
    gentest::skip("async runtime skip");
}

} // namespace async

namespace async {

gentest::async_test<void> outcome_skip_after_failure_is_fail_after_suspend() {
    co_await gentest::async::yield();
    EXPECT_TRUE(false);
    gentest::skip("async skip after failure must not mask the failure");
}

} // namespace async

namespace async {

gentest::async_test<void> waiter_lifetime_process_exit_unresumable() {
    process_exit_event.reset_all();
    co_await process_exit_event.wait("exit");
}

} // namespace async

namespace async {

gentest::async_test<void> blocked_never() {
    gentest::async::manual_event never;
    co_await never.wait("never ready");
}

} // namespace async

namespace async {

gentest::async_test<void> unsupported_suspend_after_yield() {
    co_await gentest::async::yield();
    co_await UnsupportedSuspend{};
}

} // namespace async
