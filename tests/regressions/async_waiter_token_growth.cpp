#include "../../src/async_scheduler_core.h"
#include "gentest/async.h"

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

// Regression for #264: every completed wait used to leave an expired waiter
// token entry behind until its frame finished, so one long-running case grew
// without bound. The tracked count per frame must stay bounded instead.
//
// The timer queue had the same shape of leak: a completed timed wait was only
// dropped once it reached the top of the heap, so an earlier live timer pinned
// every later-deadline entry (and its token, suspension, and composition
// state) for as long as it stayed pending.

namespace {

constexpr int         kYieldWaits = 200000;
constexpr int         kTimerWaits = 2000;
constexpr std::size_t kMaxTracked = 64;
constexpr int         kTimedWaits = 20000;
constexpr std::size_t kMaxTimers  = 64;

// Advance the scheduler clock to each pending timer instead of waiting on the
// host clock; this regression measures token retention, not timer precision.
struct ManualClock {
    using Clock                              = std::chrono::steady_clock;
    static inline Clock::duration::rep ticks = 0;
    static auto                        now() noexcept -> Clock::time_point { return Clock::time_point{Clock::duration{ticks}}; }
};

class CountingScheduler final : public gentest::detail::AsyncScheduler {
  public:
    CountingScheduler() : core_({}, &ManualClock::now) {}

    ~CountingScheduler() override {
        deactivate();
        core_.cancel_all();
    }

    auto frame_for(std::coroutine_handle<> handle) const -> gentest::detail::AsyncFramePtr override { return core_.frame_for(handle); }
    void prepare_transfer(const gentest::detail::AsyncFramePtr &frame) override {
        core_.clear_suspend_state(frame);
        gentest::detail::AsyncScheduler::prepare_transfer(frame);
    }
    void begin_wait(std::coroutine_handle<> handle) noexcept override { core_.begin_wait(handle); }
    void post(std::coroutine_handle<> handle) override { core_.post(handle); }
    void post_frame(const gentest::detail::AsyncFramePtr &frame) override { core_.post(frame); }
    auto post_completion(gentest::detail::AsyncCompletion &&completion) -> bool override {
        return core_.post_completion(std::move(completion));
    }
    void block(std::coroutine_handle<> handle, std::string reason) override { core_.block(handle, std::move(reason)); }
    void yield_at(std::coroutine_handle<> handle, const std::source_location &loc) override { core_.yield(handle, loc); }
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) override { core_.attach_child(child, parent); }
    void attach_child_frame(const gentest::detail::AsyncFramePtr &child, std::coroutine_handle<> parent) override {
        core_.attach_child(child, parent);
    }
    [[nodiscard]] auto make_waiter(std::coroutine_handle<> handle) -> WaiterTokenPtr override {
        ++waiters_created;
        return core_.make_waiter(handle, control());
    }
    void schedule_timer(std::chrono::steady_clock::time_point deadline, const WaiterTokenPtr &token) override {
        ++timers_scheduled;
        core_.schedule_timer(deadline, token);
    }

    std::size_t waiters_created   = 0;
    std::size_t timers_scheduled  = 0;
    std::size_t max_timer_entries = 0;

    // Runs the task to completion and returns the largest number of waiter
    // tokens tracked for its frame between resumptions.
    auto run_to_completion(gentest::detail::AsyncTask &task) -> std::size_t {
        gentest::detail::AsyncSchedulerScope scheduler_scope(this);
        task.set_scheduler(this);
        core_.register_frame(0, task.frame());
        core_.post(task.frame());
        const auto  handle      = task.handle();
        std::size_t max_tracked = 0;
        while (!handle.done()) {
            core_.post_due_timers();
            auto ready = core_.pop_ready();
            if (!ready) {
                if (const auto deadline = core_.next_timer_deadline()) {
                    ManualClock::ticks = deadline->time_since_epoch().count();
                    continue;
                }
                break;
            }
            auto frame = ready.frame;
            if (frame->handle && !frame->is_canceled() && !frame->done()) {
                core_.clear_suspend_state(frame);
                (void)ready.resume();
            }
            if (!handle.done()) {
                max_tracked = std::max(max_tracked, core_.tracked_waiter_tokens(handle));
            }
            max_timer_entries = std::max(max_timer_entries, core_.timer_entries());
        }
        return max_tracked;
    }

  private:
    gentest::runner::AsyncSchedulerCore core_;
};

auto many_waits() -> gentest::awaitable<void> {
    for (int i = 0; i < kYieldWaits; ++i) {
        co_await gentest::async::yield();
    }
    for (int i = 0; i < kTimerWaits; ++i) {
        // A distant deadline guarantees await_suspend runs even on slow hosts.
        co_await gentest::async::sleep_until(std::chrono::steady_clock::time_point::max());
    }
}

auto timed_operation() -> gentest::awaitable<int> {
    co_await gentest::async::yield();
    co_return 1;
}

auto timed_waits(int &completed) -> gentest::awaitable<void> {
    for (int i = 0; i < kTimedWaits; ++i) {
        const auto result = co_await gentest::async::wait_for(timed_operation(), std::chrono::hours(1));
        completed += result.ready() ? result.value() : 0;
    }
}

// The outer deadline is earlier than every inner one, so none of the completed
// inner timers can ever reach the top of the heap while the loop runs.
auto shadowed_timed_waits(int &completed) -> gentest::awaitable<void> {
    (void)co_await gentest::async::wait_for(timed_waits(completed), std::chrono::minutes(10));
}

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main() try {
    CountingScheduler scheduler;
    auto              task        = many_waits();
    const auto        max_tracked = scheduler.run_to_completion(task);
    if (!task.handle().done()) {
        return fail("waiter token growth task did not complete");
    }
    std::cout << "max tracked waiter tokens across " << (kYieldWaits + kTimerWaits) << " waits: " << max_tracked << '\n';
    if (scheduler.waiters_created != kYieldWaits + kTimerWaits || scheduler.timers_scheduled != kTimerWaits || max_tracked == 0) {
        return fail("waiter token growth task did not exercise every yield and timer suspension");
    }
    if (max_tracked > kMaxTracked) {
        return fail("completed waiter tokens were not pruned: tracked " + std::to_string(max_tracked) + " > " +
                    std::to_string(kMaxTracked));
    }

    // The first scenario advanced the manual clock to the far future; the timed
    // waits below must stay pending on the scheduler clock.
    ManualClock::ticks = 0;
    CountingScheduler timed_scheduler;
    int               completed  = 0;
    auto              timed_task = shadowed_timed_waits(completed);
    (void)timed_scheduler.run_to_completion(timed_task);
    if (!timed_task.handle().done() || completed != kTimedWaits) {
        return fail("shadowed timed waits did not complete");
    }
    std::cout << "max timer entries across " << timed_scheduler.timers_scheduled << " timed waits: " << timed_scheduler.max_timer_entries
              << '\n';
    if (timed_scheduler.timers_scheduled != kTimedWaits + 1 || timed_scheduler.max_timer_entries < 2) {
        return fail("shadowed timed waits did not schedule a timer per wait");
    }
    if (timed_scheduler.max_timer_entries > kMaxTimers) {
        return fail("completed timers were not removed: queue held " + std::to_string(timed_scheduler.max_timer_entries) + " > " +
                    std::to_string(kMaxTimers));
    }
    return 0;
} catch (const std::exception &ex) { return fail(ex.what()); } catch (...) {
    return fail("unknown exception");
}
