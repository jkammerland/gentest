#include "gentest/async.h"

#include <any>
#include <atomic>
#include <coroutine>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

static_assert(std::is_same_v<gentest::async::manual_event, gentest::async::event<std::any>>);
static_assert(!std::copy_constructible<gentest::async::future<int>>);
static_assert(std::move_constructible<gentest::async::future<int>>);
static_assert(!std::copy_constructible<gentest::async::promise<int>>);
static_assert(std::move_constructible<gentest::async::promise<int>>);

struct NoDefaultPayload {
    NoDefaultPayload() = delete;
    explicit NoDefaultPayload(int v) : value(v) {}

    int value = 0;
};

struct NoMoveAssignPayload {
    explicit NoMoveAssignPayload(int v) : value(v) {}
    NoMoveAssignPayload(NoMoveAssignPayload &&) noexcept            = default;
    NoMoveAssignPayload &operator=(NoMoveAssignPayload &&) noexcept = delete;

    int value = 0;
};

struct DefaultOnlyPayload {
    DefaultOnlyPayload() = default;

    DefaultOnlyPayload(DefaultOnlyPayload &&) noexcept            = default;
    DefaultOnlyPayload &operator=(DefaultOnlyPayload &&) noexcept = delete;

    int value = 7;
};

struct NoMoveConstructPayload {
    NoMoveConstructPayload() = default;
    explicit NoMoveConstructPayload(int v) : value(v) {}
    NoMoveConstructPayload(NoMoveConstructPayload &&) noexcept = delete;
    NoMoveConstructPayload &operator=(NoMoveConstructPayload &&other) noexcept {
        value = other.value;
        return *this;
    }

    int value = 0;
};

template <typename Event>
concept DefaultSettableEvent = requires(Event &event) { event.set("key"); };

template <typename Event, typename Value>
concept PayloadSettableEvent = requires(Event &event, Value value) { event.set("key", std::move(value)); };

static_assert(DefaultSettableEvent<gentest::async::event<int>>);
static_assert(!DefaultSettableEvent<gentest::async::event<NoDefaultPayload>>);
static_assert(!DefaultSettableEvent<gentest::async::event<DefaultOnlyPayload>>);
static_assert(PayloadSettableEvent<gentest::async::event<NoDefaultPayload>, NoDefaultPayload>);
static_assert(!PayloadSettableEvent<gentest::async::event<NoMoveAssignPayload>, NoMoveAssignPayload>);
static_assert(!PayloadSettableEvent<gentest::async::event<DefaultOnlyPayload>, DefaultOnlyPayload>);
static_assert(PayloadSettableEvent<gentest::async::event<NoMoveConstructPayload>, int>);

class TrackingScheduler final : public gentest::detail::AsyncScheduler {
  public:
    void post(std::coroutine_handle<> handle) override {
        std::lock_guard<std::mutex> lk(mtx_);
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
        auto                        token = AsyncScheduler::make_waiter(handle);
        std::lock_guard<std::mutex> lk(mtx_);
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
        while (true) {
            std::coroutine_handle<> handle;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                if (ready_.empty()) {
                    return;
                }
                handle = ready_.front();
                ready_.pop_front();
            }
            if (handle && !handle.done()) {
                handle.resume();
            }
        }
    }

    void cancel_waiters() {
        std::vector<WaiterTokenPtr> waiters;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            waiters.swap(waiters_);
            record_late_posts_ = true;
        }
        for (auto &waiter : waiters) {
            waiter->cancel();
        }
    }

    void record_late_posts_without_canceling_waiters() {
        std::lock_guard<std::mutex> lk(mtx_);
        record_late_posts_ = true;
    }

    [[nodiscard]] auto late_posts() const -> int {
        std::lock_guard<std::mutex> lk(mtx_);
        return late_posts_;
    }

  private:
    mutable std::mutex                  mtx_;
    std::deque<std::coroutine_handle<>> ready_;
    std::vector<WaiterTokenPtr>         waiters_;
    bool                                record_late_posts_ = false;
    int                                 late_posts_        = 0;
};

auto wait_for_manual_event(gentest::async::manual_event &event) -> gentest::async_test<void> { co_await event.wait("ready"); }

auto wait_for_manual_event_key(gentest::async::manual_event &event, std::string key) -> gentest::async_test<std::any *> {
    std::any &payload = co_await event.wait(std::move(key));
    co_return &payload;
}

auto wait_for_int_event_key(gentest::async::event<int> &event, std::string key) -> gentest::async_test<int *> {
    int &payload = co_await event.wait(std::move(key));
    co_return &payload;
}

auto wait_for_string_event_key(gentest::async::event<std::string> &event, std::string key) -> gentest::async_test<std::string *> {
    std::string &payload = co_await event.wait(std::move(key));
    co_return &payload;
}

auto wait_for_no_default_event_key(gentest::async::event<NoDefaultPayload> &event, std::string key)
    -> gentest::async_test<NoDefaultPayload *> {
    NoDefaultPayload &payload = co_await event.wait(std::move(key));
    co_return &payload;
}

auto wait_for_no_move_construct_event_key(gentest::async::event<NoMoveConstructPayload> &event, std::string key)
    -> gentest::async_test<NoMoveConstructPayload *> {
    NoMoveConstructPayload &payload = co_await event.wait(std::move(key));
    co_return &payload;
}

auto wait_for_promise(gentest::async::future<void> future) -> gentest::async_test<void> {
    co_await future.wait("promise stale waiter regression");
}

auto wait_for_int_future(gentest::async::future<int> future) -> gentest::async_test<int> {
    co_return co_await future.wait("promise int payload regression");
}

auto wait_for_string_future(gentest::async::future<std::string> future) -> gentest::async_test<std::string> {
    co_return co_await future.wait("promise string payload regression");
}

auto wait_for_unique_ptr_future(gentest::async::future<std::unique_ptr<int>> future) -> gentest::async_test<std::unique_ptr<int>> {
    co_return co_await future.wait("promise move-only payload regression");
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

int main() try {
    {
        gentest::async::event<int> event;
        TrackingScheduler          scheduler;
        event.set("answer", 42);
        auto task = wait_for_int_event_key(event, "answer");
        scheduler.run_until_blocked(task);
        if (!task.handle().done()) {
            return fail("event<int> pre-set key did not resume immediately");
        }
        if (*task.await_resume() != 42) {
            return fail("event<int> returned the wrong payload");
        }
    }

    {
        gentest::async::event<int> event;
        TrackingScheduler          scheduler;
        event.set("zero");
        auto task = wait_for_int_event_key(event, "zero");
        scheduler.run_until_blocked(task);
        if (!task.handle().done() || *task.await_resume() != 0) {
            return fail("event<int> default set did not return a value-initialized payload");
        }
    }

    {
        gentest::async::event<std::string> event;
        TrackingScheduler                  scheduler;
        event.set("alpha", "initial");
        auto first = wait_for_string_event_key(event, "alpha");
        scheduler.run_until_blocked(first);
        if (!first.handle().done()) {
            return fail("event<string> stable slot pre-set wait did not resume");
        }
        auto *first_payload = first.await_resume();
        if (*first_payload != "initial") {
            return fail("event<string> returned the wrong initial payload");
        }

        *first_payload = "mutated";
        auto second    = wait_for_string_event_key(event, "alpha");
        scheduler.run_until_blocked(second);
        auto *second_payload = second.await_resume();
        if (!second.handle().done() || second_payload != first_payload || *second_payload != "mutated") {
            return fail("event<string> wait did not return the stable mutated key slot");
        }

        event.reset("alpha");
        auto third = wait_for_string_event_key(event, "alpha");
        scheduler.run_until_blocked(third);
        if (third.handle().done()) {
            return fail("event<string> reset(key) did not suspend later waiter");
        }
        event.set("alpha", std::string("new"));
        scheduler.run_ready();
        if (!third.handle().done()) {
            return fail("event<string> stable slot did not resume after reset and set");
        }
        auto *third_payload = third.await_resume();
        if (third_payload != first_payload || *third_payload != "new") {
            return fail("event<string> reset and set did not preserve the stable key slot");
        }
    }

    {
        gentest::async::event<NoDefaultPayload> event;
        TrackingScheduler                       scheduler;
        event.set("payload", NoDefaultPayload{17});
        auto task = wait_for_no_default_event_key(event, "payload");
        scheduler.run_until_blocked(task);
        if (!task.handle().done() || task.await_resume()->value != 17) {
            return fail("event<NoDefaultPayload> did not accept an explicit payload");
        }
    }

    {
        gentest::async::event<NoMoveConstructPayload> event;
        TrackingScheduler                             scheduler;
        event.set("payload", 19);
        event.set("payload", 20);
        auto task = wait_for_no_move_construct_event_key(event, "payload");
        scheduler.run_until_blocked(task);
        if (!task.handle().done() || task.await_resume()->value != 20) {
            return fail("event<NoMoveConstructPayload> did not construct and update an explicit payload");
        }
    }

    {
        gentest::async::event<NoDefaultPayload> event;
        TrackingScheduler                       scheduler;
        auto                                    first = wait_for_no_default_event_key(event, "late");
        scheduler.run_until_blocked(first);
        if (first.handle().done()) {
            return fail("event<NoDefaultPayload> completed before explicit payload set");
        }

        event.set("late", NoDefaultPayload{23});
        scheduler.run_ready();
        if (!first.handle().done() || first.await_resume()->value != 23) {
            return fail("event<NoDefaultPayload> did not resume wait-before-set payload");
        }
        auto *first_payload = first.await_resume();

        event.reset("late");
        auto second = wait_for_no_default_event_key(event, "late");
        scheduler.run_until_blocked(second);
        if (second.handle().done()) {
            return fail("event<NoDefaultPayload> reset did not suspend later waiter");
        }

        event.set("late", NoDefaultPayload{24});
        scheduler.run_ready();
        if (!second.handle().done()) {
            return fail("event<NoDefaultPayload> did not resume after reset and explicit set");
        }
        auto *second_payload = second.await_resume();
        if (second_payload != first_payload || second_payload->value != 24) {
            return fail("event<NoDefaultPayload> reset and set did not preserve stable slot");
        }
    }

    {
        gentest::async::manual_event event;
        TrackingScheduler            scheduler;
        event.set("alpha", std::string("pre-set"));
        auto task = wait_for_manual_event_key(event, "alpha");
        scheduler.run_until_blocked(task);
        if (!task.handle().done()) {
            return fail("manual_event pre-set key did not resume immediately");
        }
        if (!any_value_is(*task.await_resume(), std::string("pre-set"))) {
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
        if (!any_value_is(*task.await_resume(), 42)) {
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
        if (!task.handle().done() || !any_value_is(*task.await_resume(), 2)) {
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
        auto *first_payload  = first.await_resume();
        auto *second_payload = second.await_resume();
        if (first_payload != second_payload) {
            return fail("manual_event broadcast did not return the shared key slot");
        }
        if (!any_value_is(*first_payload, std::string("broadcast")) || !any_value_is(*second_payload, std::string("broadcast"))) {
            return fail("manual_event broadcast returned the wrong payload");
        }
    }

    {
        gentest::async::manual_event event;
        TrackingScheduler            scheduler;
        event.set("alpha", std::string("initial"));
        auto first = wait_for_manual_event_key(event, "alpha");
        scheduler.run_until_blocked(first);
        if (!first.handle().done()) {
            return fail("manual_event stable slot pre-set wait did not resume");
        }
        auto *first_payload = first.await_resume();
        if (!any_value_is(*first_payload, std::string("initial"))) {
            return fail("manual_event stable slot returned the wrong initial payload");
        }

        std::any_cast<std::string &>(*first_payload) = "mutated";
        auto second                                  = wait_for_manual_event_key(event, "alpha");
        scheduler.run_until_blocked(second);
        if (!second.handle().done()) {
            return fail("manual_event stable slot second wait did not resume");
        }
        auto *second_payload = second.await_resume();
        if (second_payload != first_payload || !any_value_is(*second_payload, std::string("mutated"))) {
            return fail("manual_event wait did not return the stable mutated key slot");
        }

        event.reset("alpha");
        if (event.is_set("alpha")) {
            return fail("manual_event reset(key) left stable slot ready");
        }
        if (!any_value_is(*first_payload, std::string("mutated"))) {
            return fail("manual_event reset(key) invalidated the stable payload slot");
        }

        auto third = wait_for_manual_event_key(event, "alpha");
        scheduler.run_until_blocked(third);
        if (third.handle().done()) {
            return fail("manual_event reset(key) did not suspend later waiter");
        }
        event.set("alpha", std::string("new"));
        scheduler.run_ready();
        if (!third.handle().done()) {
            return fail("manual_event stable slot did not resume after reset and set");
        }
        auto *third_payload = third.await_resume();
        if (third_payload != first_payload || !any_value_is(*third_payload, std::string("new")) ||
            !any_value_is(*first_payload, std::string("new"))) {
            return fail("manual_event reset and set did not preserve the stable key slot");
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
        if (!beta.handle().done() || !any_value_is(*beta.await_resume(), 2)) {
            return fail("manual_event reset(key) cleared another key");
        }
        event.set("alpha", 3);
        scheduler.run_ready();
        if (!alpha.handle().done() || !any_value_is(*alpha.await_resume(), 3)) {
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
        if (!alpha.handle().done() || alpha.await_resume()->has_value()) {
            return fail("manual_event empty payload should return empty std::any");
        }
        if (!beta.handle().done() || !any_value_is(*beta.await_resume(), 4)) {
            return fail("manual_event reset_all did not accept later sets");
        }
    }

    {
        gentest::async::manual_event event;
        TrackingScheduler            scheduler;
        auto                         alpha = wait_for_manual_event_key(event, "alpha");
        auto                         beta  = wait_for_manual_event_key(event, "beta");
        scheduler.run_until_blocked(alpha);
        scheduler.run_until_blocked(beta);
        event.reset("alpha");
        event.reset_all();
        event.set("alpha", 1);
        event.set("beta", 2);
        scheduler.run_ready();
        if (!alpha.handle().done() || !beta.handle().done()) {
            return fail("manual_event reset/reset_all unregistered suspended waiters");
        }
        if (!any_value_is(*alpha.await_resume(), 1) || !any_value_is(*beta.await_resume(), 2)) {
            return fail("manual_event reset/reset_all waiters resumed with wrong payload");
        }
    }

    {
        gentest::async::manual_event event;
        TrackingScheduler            scheduler;
        event.set("replace", std::string("text"));
        auto first = wait_for_manual_event_key(event, "replace");
        scheduler.run_until_blocked(first);
        if (!first.handle().done() || !any_value_is(*first.await_resume(), std::string("text"))) {
            return fail("manual_event heterogeneous replacement did not return initial payload");
        }
        auto *slot = first.await_resume();

        event.set("replace", 17);
        auto second = wait_for_manual_event_key(event, "replace");
        scheduler.run_until_blocked(second);
        if (!second.handle().done() || second.await_resume() != slot || !any_value_is(*slot, 17)) {
            return fail("manual_event heterogeneous replacement did not reuse stable any slot");
        }
    }

    {
        gentest::async::manual_event event;
        TrackingScheduler            scheduler;
        auto                         task = gentest::detail::make_async_task(wait_for_manual_event(event));
        scheduler.run_until_blocked(*task);
        scheduler.record_late_posts_without_canceling_waiters();
        task.reset();
        event.set("ready");
        if (scheduler.late_posts() != 0) {
            return fail("manual_event posted an expired stale waiter");
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
        gentest::async::manual_event event;
        TrackingScheduler            scheduler;
        auto                         stale = gentest::detail::make_async_task(wait_for_manual_event(event));
        scheduler.run_until_blocked(*stale);
        stale.reset();

        auto live = wait_for_manual_event_key(event, "ready");
        scheduler.run_until_blocked(live);
        if (live.handle().done()) {
            return fail("manual_event mixed stale/live waiter completed before set");
        }
        event.set("ready", 5);
        scheduler.run_ready();
        if (!live.handle().done() || !any_value_is(*live.await_resume(), 5)) {
            return fail("manual_event mixed stale/live waiter did not resume live waiter");
        }
    }

    {
        gentest::async::manual_event event;
        constexpr int                kWaiters = 8;
        std::atomic<int>             registered{0};
        std::atomic<bool>            release{false};
        std::atomic<int>             failures{0};
        std::vector<std::any *>      payloads(static_cast<std::size_t>(kWaiters), nullptr);
        std::vector<std::thread>     threads;
        threads.reserve(static_cast<std::size_t>(kWaiters));

        for (int i = 0; i != kWaiters; ++i) {
            threads.emplace_back([&, i] {
                TrackingScheduler scheduler;
                auto              task = wait_for_manual_event_key(event, "shared");
                scheduler.run_until_blocked(task);
                if (task.handle().done()) {
                    failures.fetch_add(1, std::memory_order_acq_rel);
                    return;
                }
                registered.fetch_add(1, std::memory_order_acq_rel);
                while (!release.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                scheduler.run_ready();
                if (!task.handle().done()) {
                    failures.fetch_add(1, std::memory_order_acq_rel);
                    return;
                }
                auto *payload                         = task.await_resume();
                payloads[static_cast<std::size_t>(i)] = payload;
                if (!any_value_is(*payload, 99)) {
                    failures.fetch_add(1, std::memory_order_acq_rel);
                }
            });
        }

        while (registered.load(std::memory_order_acquire) != kWaiters) {
            std::this_thread::yield();
        }
        event.set("shared", 99);
        release.store(true, std::memory_order_release);

        for (auto &thread : threads) {
            thread.join();
        }
        if (failures.load(std::memory_order_acquire) != 0) {
            return fail("manual_event concurrent waiters did not all resume from a threaded set");
        }
        for (auto *payload : payloads) {
            if (payload != payloads.front()) {
                return fail("manual_event concurrent waiters did not share the stable key slot");
            }
        }
    }

    {
        gentest::async::manual_event event;
        constexpr int                kWaiters = 8;
        std::atomic<int>             registered{0};
        std::atomic<bool>            done_resetting{false};
        std::atomic<int>             failures{0};
        std::vector<std::thread>     threads;
        threads.reserve(static_cast<std::size_t>(kWaiters) + 1U);

        for (int i = 0; i != kWaiters; ++i) {
            threads.emplace_back([&, i] {
                TrackingScheduler scheduler;
                auto              task = wait_for_manual_event_key(event, "shared.reset");
                scheduler.run_until_blocked(task);
                registered.fetch_add(1, std::memory_order_acq_rel);
                while (!done_resetting.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                scheduler.run_ready();
                if (!task.handle().done() || !any_value_is(*task.await_resume(), 123)) {
                    failures.fetch_add(1, std::memory_order_acq_rel);
                }
            });
        }

        threads.emplace_back([&] {
            while (registered.load(std::memory_order_acquire) != kWaiters) {
                std::this_thread::yield();
            }
            for (int i = 0; i != 1000; ++i) {
                if (i % 2 == 0) {
                    event.reset("shared.reset");
                } else {
                    event.reset_all();
                }
            }
            event.set("shared.reset", 123);
            done_resetting.store(true, std::memory_order_release);
        });

        for (auto &thread : threads) {
            thread.join();
        }
        if (failures.load(std::memory_order_acquire) != 0) {
            return fail("manual_event suspended waiters did not survive concurrent reset/set");
        }
    }

    {
        gentest::async::manual_event event;
        std::vector<std::thread>     threads;
        threads.reserve(4);
        for (int thread_index = 0; thread_index != 4; ++thread_index) {
            threads.emplace_back([&, thread_index] {
                for (int i = 0; i != 1000; ++i) {
                    const auto key = std::string("stress.") + std::to_string((thread_index + i) % 8);
                    event.set(key, i);
                    (void)event.is_set(key);
                    if (i % 3 == 0) {
                        event.reset(key);
                    }
                    if (i % 97 == 0) {
                        event.reset_all();
                    }
                }
            });
        }
        for (auto &thread : threads) {
            thread.join();
        }
    }

    {
        gentest::async::promise<int> promise;
        auto                         future = promise.get_future();
        promise.set_value(42);
        TrackingScheduler scheduler;
        auto              task = wait_for_int_future(std::move(future));
        scheduler.run_until_blocked(task);
        if (!task.handle().done() || task.await_resume() != 42) {
            return fail("promise<int> pre-set value did not resume immediately");
        }
    }

    {
        gentest::async::promise<int> promise;
        auto                         future = promise.get_future();
        TrackingScheduler            scheduler;
        auto                         task = wait_for_int_future(std::move(future));
        scheduler.run_until_blocked(task);
        if (task.handle().done()) {
            return fail("promise<int> wait-before-set completed too early");
        }
        std::thread worker([&promise] { promise.set_value(99); });
        worker.join();
        scheduler.run_ready();
        if (!task.handle().done() || task.await_resume() != 99) {
            return fail("promise<int> threaded set did not resume waiter");
        }
    }

    {
        gentest::async::promise<std::unique_ptr<int>> promise;
        auto                                          future = promise.get_future();
        promise.set_value(std::make_unique<int>(7));
        TrackingScheduler scheduler;
        auto              task = wait_for_unique_ptr_future(std::move(future));
        scheduler.run_until_blocked(task);
        auto payload = task.await_resume();
        if (!task.handle().done() || !payload || *payload != 7) {
            return fail("promise<unique_ptr<int>> did not move payload through future");
        }
    }

    {
        gentest::async::promise<std::string> promise;
        auto                                 future = promise.get_future();
        if (!promise.try_set_value("first")) {
            return fail("promise<string> first try_set_value failed");
        }
        if (promise.try_set_value("second")) {
            return fail("promise<string> allowed duplicate try_set_value");
        }
        if (promise.try_set_blocked("late blocked")) {
            return fail("promise<string> allowed blocked state after value");
        }
        TrackingScheduler scheduler;
        auto              task = wait_for_string_future(std::move(future));
        scheduler.run_until_blocked(task);
        if (!task.handle().done() || task.await_resume() != "first") {
            return fail("promise<string> first terminal value did not win");
        }
    }

    {
        gentest::async::promise<int> promise;
        (void)promise.get_future();
        try {
            (void)promise.get_future();
            return fail("promise allowed get_future twice");
        } catch (const std::logic_error &ex) {
            if (std::string_view(ex.what()).empty()) {
                return fail("promise get_future error had no diagnostic");
            }
        }
    }

    {
        gentest::async::promise<int> promise;
        auto                         future = promise.get_future();
        promise.set_exception(std::make_exception_ptr(std::runtime_error("promise exception marker")));
        TrackingScheduler scheduler;
        auto              task = wait_for_int_future(std::move(future));
        scheduler.run_until_blocked(task);
        try {
            (void)task.await_resume();
            return fail("promise exception did not rethrow from future");
        } catch (const std::runtime_error &ex) {
            if (std::string_view(ex.what()).find("promise exception marker") == std::string_view::npos) {
                return fail("promise exception rethrew the wrong message");
            }
        }
    }

    {
        gentest::async::promise<void> promise;
        auto                          future = promise.get_future();
        promise.set_blocked("promise blocked marker");
        TrackingScheduler scheduler;
        auto              task = wait_for_promise(std::move(future));
        scheduler.run_until_blocked(task);
        try {
            task.await_resume();
            return fail("blocked promise did not block future");
        } catch (const gentest::detail::blocked_exception &ex) {
            if (ex.reason() != "promise blocked marker") {
                return fail("blocked promise reported the wrong reason");
            }
        }
    }

    {
        gentest::async::future<void> future;
        {
            gentest::async::promise<void> promise;
            future = promise.get_future();
        }
        TrackingScheduler scheduler;
        auto              task = wait_for_promise(std::move(future));
        scheduler.run_until_blocked(task);
        try {
            task.await_resume();
            return fail("broken promise did not block future");
        } catch (const gentest::detail::blocked_exception &ex) {
            if (ex.reason() != "async promise was abandoned before completion") {
                return fail("broken promise reported the wrong reason");
            }
        }
    }

    {
        gentest::async::promise<void> promise;
        auto                          future  = promise.get_future();
        auto                          awaiter = future.wait("first waiter");
        (void)awaiter;
        try {
            (void)future.wait("second waiter");
            return fail("future allowed multiple waiters");
        } catch (const std::logic_error &ex) {
            if (std::string_view(ex.what()).empty()) {
                return fail("future multiple waiter error had no diagnostic");
            }
        }
        promise.set_value();
    }

    {
        gentest::async::promise<void> promise;
        TrackingScheduler             scheduler;
        auto                          future = promise.get_future();
        auto                          task   = gentest::detail::make_async_task(wait_for_promise(std::move(future)));
        scheduler.run_until_blocked(*task);
        scheduler.cancel_waiters();
        task.reset();
        promise.set_value();
        if (scheduler.late_posts() != 0) {
            return fail("promise posted a canceled stale waiter");
        }
    }

    return 0;
} catch (const std::exception &ex) { return fail(ex.what()); } catch (...) {
    return fail("unknown exception");
}
