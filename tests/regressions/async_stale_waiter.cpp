#include "gentest/async.h"

#include <any>
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
        run_ready();
    }

    void run_ready() {
        gentest::detail::AsyncSchedulerScope scheduler_scope(this);
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

auto wait_for_manual_event(gentest::async::manual_event &event) -> gentest::async_test<void> { co_await event.wait("ready"); }

auto wait_for_manual_event_key(gentest::async::manual_event &event, std::string key) -> gentest::async_test<std::any> {
    co_return co_await event.wait(std::move(key));
}

auto wait_for_completion_source(gentest::async::completion_source &source) -> gentest::async_test<void> {
    co_await source.wait("completion source stale waiter regression");
}

auto fail(std::string_view message) -> int {
    std::cerr << message << '\n';
    return 1;
}

template <typename T> auto any_value_is(const std::any &value, const T &expected) -> bool {
    const auto *actual = std::any_cast<T>(&value);
    return actual != nullptr && *actual == expected;
}

} // namespace

int main() {
    {
        gentest::async::manual_event event;
        TrackingScheduler            scheduler;
        event.set("alpha", std::string("pre-set"));
        auto task = wait_for_manual_event_key(event, "alpha");
        scheduler.run_until_blocked(task);
        if (!task.handle().done()) {
            return fail("manual_event pre-set key did not resume immediately");
        }
        if (!any_value_is(task.await_resume(), std::string("pre-set"))) {
            return fail("manual_event pre-set key returned the wrong payload");
        }
    }

    {
        gentest::async::manual_event event;
        TrackingScheduler            scheduler;
        auto                         task = wait_for_manual_event_key(event, "alpha");
        scheduler.run_until_blocked(task);
        if (task.handle().done()) {
            return fail("manual_event waiter completed before key was set");
        }
        event.set("alpha", 42);
        scheduler.run_ready();
        if (!task.handle().done()) {
            return fail("manual_event set key did not resume waiter");
        }
        if (!any_value_is(task.await_resume(), 42)) {
            return fail("manual_event waiter returned the wrong payload");
        }
    }

    {
        gentest::async::manual_event event;
        TrackingScheduler            scheduler;
        auto                         task = wait_for_manual_event_key(event, "alpha");
        scheduler.run_until_blocked(task);
        event.set("beta", 1);
        scheduler.run_ready();
        if (task.handle().done()) {
            return fail("manual_event resumed waiter for the wrong key");
        }
        event.set("alpha", 2);
        scheduler.run_ready();
        if (!task.handle().done() || !any_value_is(task.await_resume(), 2)) {
            return fail("manual_event did not resume waiter for the matching key");
        }
    }

    {
        gentest::async::manual_event event;
        TrackingScheduler            scheduler;
        auto                         first  = wait_for_manual_event_key(event, "alpha");
        auto                         second = wait_for_manual_event_key(event, "alpha");
        scheduler.run_until_blocked(first);
        scheduler.run_until_blocked(second);
        event.set("alpha", std::string("broadcast"));
        scheduler.run_ready();
        if (!first.handle().done() || !second.handle().done()) {
            return fail("manual_event did not broadcast to all waiters for a key");
        }
        if (!any_value_is(first.await_resume(), std::string("broadcast")) ||
            !any_value_is(second.await_resume(), std::string("broadcast"))) {
            return fail("manual_event broadcast returned the wrong payload");
        }
    }

    {
        gentest::async::manual_event event;
        TrackingScheduler            scheduler;
        event.set("alpha", 1);
        event.set("beta", 2);
        event.reset("alpha");
        auto alpha = wait_for_manual_event_key(event, "alpha");
        auto beta  = wait_for_manual_event_key(event, "beta");
        scheduler.run_until_blocked(alpha);
        scheduler.run_until_blocked(beta);
        if (alpha.handle().done()) {
            return fail("manual_event reset(key) left key ready");
        }
        if (!beta.handle().done() || !any_value_is(beta.await_resume(), 2)) {
            return fail("manual_event reset(key) cleared another key");
        }
        event.set("alpha", 3);
        scheduler.run_ready();
        if (!alpha.handle().done() || !any_value_is(alpha.await_resume(), 3)) {
            return fail("manual_event reset key did not accept a later set");
        }
    }

    {
        gentest::async::manual_event event;
        TrackingScheduler            scheduler;
        event.set("alpha", 1);
        event.set("beta", 2);
        event.reset_all();
        auto alpha = wait_for_manual_event_key(event, "alpha");
        auto beta  = wait_for_manual_event_key(event, "beta");
        scheduler.run_until_blocked(alpha);
        scheduler.run_until_blocked(beta);
        if (alpha.handle().done() || beta.handle().done()) {
            return fail("manual_event reset_all left a key ready");
        }
        event.set("alpha", {});
        event.set("beta", 4);
        scheduler.run_ready();
        if (!alpha.handle().done() || alpha.await_resume().has_value()) {
            return fail("manual_event empty payload should return empty std::any");
        }
        if (!beta.handle().done() || !any_value_is(beta.await_resume(), 4)) {
            return fail("manual_event reset_all did not accept later sets");
        }
    }

    {
        gentest::async::manual_event event;
        TrackingScheduler            scheduler;
        auto                         task = gentest::detail::make_async_task(wait_for_manual_event(event));
        scheduler.run_until_blocked(*task);
        scheduler.cancel_waiters();
        task.reset();
        event.set("ready");
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
