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
#include <thread>

// Regression for #264: every completed wait used to leave an expired waiter
// token entry behind until its frame finished, so one long-running case grew
// without bound. The tracked count per frame must stay bounded instead.

namespace {

constexpr int         kYieldWaits = 200000;
constexpr int         kTimerWaits = 2000;
constexpr std::size_t kMaxTracked = 64;

class CountingScheduler final : public gentest::detail::AsyncScheduler {
  public:
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
        return core_.make_waiter(handle, control());
    }
    void schedule_timer(std::chrono::steady_clock::time_point deadline, const WaiterTokenPtr &token) override {
        core_.schedule_timer(deadline, token);
    }

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
                    std::this_thread::sleep_until(*deadline);
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
        co_await gentest::async::sleep_for(std::chrono::microseconds(50));
    }
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
    if (max_tracked > kMaxTracked) {
        return fail("completed waiter tokens were not pruned: tracked " + std::to_string(max_tracked) + " > " +
                    std::to_string(kMaxTracked));
    }
    return 0;
} catch (const std::exception &ex) { return fail(ex.what()); } catch (...) {
    return fail("unknown exception");
}
