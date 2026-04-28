#include "gentest/async.h"

#include <coroutine>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

class TrackingScheduler final : public gentest::detail::AsyncScheduler {
  public:
    void post(std::coroutine_handle<> handle) override {
        if (record_late_posts_) {
            ++late_posts_;
            return;
        }
        ready_.push_back(handle);
    }

    void block(std::coroutine_handle<> handle, std::string reason) override {
        (void)handle;
        (void)reason;
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) override {
        (void)child;
        (void)parent;
    }

    [[nodiscard]] auto make_waiter(std::coroutine_handle<> handle) -> WaiterTokenPtr override {
        auto token = AsyncScheduler::make_waiter(handle);
        waiters_.push_back(token);
        return token;
    }

    void run_until_blocked(gentest::detail::AsyncTask &task) {
        gentest::detail::AsyncSchedulerScope scheduler_scope(this);
        task.set_scheduler(this);
        post(task.handle());
        while (!ready_.empty()) {
            auto handle = ready_.front();
            ready_.pop_front();
            if (handle && !handle.done()) {
                handle.resume();
            }
        }
    }

    void cancel_waiters() {
        for (auto &waiter : waiters_) {
            waiter->cancel();
        }
        waiters_.clear();
        record_late_posts_ = true;
    }

    [[nodiscard]] auto late_posts() const -> int { return late_posts_; }

  private:
    std::deque<std::coroutine_handle<>> ready_;
    std::vector<WaiterTokenPtr>         waiters_;
    bool                                record_late_posts_ = false;
    int                                 late_posts_        = 0;
};

auto wait_for_manual_event(gentest::async::manual_event &event) -> gentest::async_test<void> {
    co_await event.wait("manual event stale waiter regression");
}

auto wait_for_completion_source(gentest::async::completion_source &source) -> gentest::async_test<void> {
    co_await source.wait("completion source stale waiter regression");
}

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

} // namespace

int main() {
    {
        gentest::async::manual_event event;
        TrackingScheduler            scheduler;
        auto                         task = gentest::detail::make_async_task(wait_for_manual_event(event));
        scheduler.run_until_blocked(*task);
        scheduler.cancel_waiters();
        task.reset();
        event.set();
        if (scheduler.late_posts() != 0) {
            return fail("manual_event posted a canceled stale waiter");
        }
    }

    {
        gentest::async::completion_source source;
        TrackingScheduler                 scheduler;
        auto                              task = gentest::detail::make_async_task(wait_for_completion_source(source));
        scheduler.run_until_blocked(*task);
        scheduler.cancel_waiters();
        task.reset();
        source.complete();
        if (scheduler.late_posts() != 0) {
            return fail("completion_source posted a canceled stale waiter");
        }
    }

    return 0;
}
