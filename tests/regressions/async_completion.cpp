#include "../../src/async_scheduler_core.h"
#include "gentest/detail/registration_runtime.h"
#include "gentest/detail/runtime_context.h"
#include "gentest/runner.h"

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <future>
#include <latch>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using namespace gentest::asserts;
using TimePoint = std::chrono::steady_clock::time_point;
using Token     = gentest::detail::AsyncScheduler::WaiterTokenPtr;

struct ManualClock {
    static inline std::atomic<int> milliseconds{0};
    static auto                    now() noexcept -> TimePoint { return TimePoint{std::chrono::milliseconds{milliseconds.load()}}; }
};

class Scheduler final : public gentest::detail::AsyncScheduler {
  public:
    Scheduler() : core_({}, &ManualClock::now) {}
    ~Scheduler() override {
        deactivate();
        core_.cancel_all();
    }

    void post(std::coroutine_handle<> handle) override { core_.post(handle); }
    void post_frame(const gentest::detail::AsyncFramePtr &frame) override { core_.post(frame); }
    auto post_completion(gentest::detail::AsyncCompletion &&completion) -> bool override {
        if (before_admission) {
            before_admission();
        }
        return core_.post_completion(std::move(completion));
    }
    void block(std::coroutine_handle<> handle, std::string reason) override { core_.block(handle, std::move(reason)); }
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void attach_child(std::coroutine_handle<> child, std::coroutine_handle<> parent) override { core_.attach_child(child, parent); }
    void attach_child_frame(const gentest::detail::AsyncFramePtr &child, std::coroutine_handle<> parent) override {
        core_.attach_child(child, parent);
    }
    auto make_waiter(std::coroutine_handle<> handle) -> Token override { return core_.make_waiter(handle, control()); }
    void schedule_timer(TimePoint deadline, const Token &token) override { timers_.emplace_back(deadline, token); }
    void cancel_waiters(std::coroutine_handle<> handle) noexcept override { core_.cancel_waiters(handle); }

    void start(gentest::detail::AsyncTask &task) {
        task.set_scheduler(this);
        core_.register_frame(0, task.frame());
        core_.post(task.frame());
    }
    auto pump() -> unsigned {
        gentest::detail::AsyncSchedulerScope scope(this);
        unsigned                             count = 0;
        while (auto ready = core_.pop_ready()) {
            count += ready.resume() ? 1U : 0U;
        }
        return count;
    }
    void fire_timers() {
        for (auto &[deadline, token] : timers_) {
            if (deadline <= ManualClock::now()) {
                token->post();
            }
        }
    }
    void cancel_all() { core_.cancel_all(); }

    std::function<void()> before_admission;

  private:
    gentest::runner::AsyncSchedulerCore      core_;
    std::vector<std::pair<TimePoint, Token>> timers_;
};

enum class Reason { Received, Disconnected };
struct Reply {
    Reason               reason;
    std::unique_ptr<int> value;
};
using Callback = gentest::async::continuation<Reason, std::unique_ptr<int>>;
static_assert(!std::copy_constructible<Callback>);
static_assert(std::copy_constructible<gentest::async::continuation_handle>);

struct Source {
    Callback                            callback;
    Callback                            alternate;
    gentest::async::continuation_handle next;
    std::stop_token                     stop;
    std::optional<Reply>                initial;
    std::function<void()>               on_publish;
    std::thread::id                     publisher;
    unsigned                            published           = 0;
    unsigned                            consumed            = 0;
    unsigned                            canceled            = 0;
    bool                                inline_completion   = false;
    bool                                registration_throws = false;
    std::promise<void>                 *registered          = nullptr;
};

struct Receive {
    Source              &source;
    std::optional<Reply> result;

    auto await_ready() -> bool {
        if (!source.initial) {
            return false;
        }
        result = std::move(source.initial);
        return true;
    }
    template <typename Promise> void await_suspend(std::coroutine_handle<Promise> parent) {
        auto next = parent.promise().make_continuation();
        auto stop = next.stop_token();
        await_subscribe(std::move(next), stop);
    }
    void await_subscribe(gentest::async::continuation_handle next, std::stop_token stop) {
        source.next   = next;
        source.stop   = std::move(stop);
        auto receiver = [this](Reason reason, std::unique_ptr<int> value) noexcept {
            result.emplace(Reply{.reason = reason, .value = std::move(value)});
            ++source.published;
            source.publisher = std::this_thread::get_id();
            if (source.on_publish) {
                source.on_publish();
            }
        };
        source.callback  = gentest::async::bind_continuation<Reason, std::unique_ptr<int>>(next, receiver);
        source.alternate = gentest::async::bind_continuation<Reason, std::unique_ptr<int>>(std::move(next), receiver);
        if (source.inline_completion) {
            source.callback(Reason::Received, std::make_unique<int>(31));
        }
        if (source.registration_throws) {
            throw std::runtime_error("registration failure");
        }
        if (source.registered) {
            source.registered->set_value();
        }
    }
    auto await_resume() -> Reply {
        ++source.consumed;
        if (result) {
            return std::move(*result);
        }
        throw std::logic_error("resumed without a published reply");
    }
    void cancel_wait() noexcept {
        ++source.canceled;
        result.reset();
    }
};

auto receive(Source &source, std::optional<Reply> &output) -> gentest::async_test<void> {
    output = co_await Receive{.source = source, .result = {}};
}

auto timed_receive(Source &source, gentest::async::wait_status &status, std::optional<Reply> &output) -> gentest::async_test<void> {
    auto result = co_await gentest::async::timeout_detail::timeout_awaitable<Receive, ManualClock>{
        Receive{.source = source, .result = {}}, TimePoint{100ms}, std::source_location::current()};
    status = result.status();
    if (result.ready()) {
        output.emplace(std::move(result).value());
    }
}

auto empty_and_void_callbacks() -> bool {
    gentest::async::continuation_handle empty;
    unsigned                            published = 0;
    if (empty || empty.try_invoke([&]() noexcept { ++published; })) {
        return false;
    }

    struct VoidWait {
        gentest::async::continuation<> &callback;
        unsigned                       &count;
        auto                            await_ready() const noexcept -> bool { return false; }
        void                            await_suspend(std::coroutine_handle<gentest::async_test<void>::promise_type> parent) {
            callback = gentest::async::bind_continuation<>(parent.promise().make_continuation(), [this]() noexcept { ++count; });
        }
        void await_resume() const noexcept {}
    };
    gentest::async::continuation<> callback;
    auto                           operation = [](auto &completion, unsigned &count) -> gentest::async_test<void> {
        co_await VoidWait{.callback = completion, .count = count};
    };
    auto      task = operation(callback, published);
    Scheduler scheduler;
    scheduler.start(task);
    if (scheduler.pump() != 1) {
        return false;
    }
    callback();
    callback();
    if (published != 0) {
        return false;
    }
    return scheduler.pump() == 1 && task.handle().done() && !task.exception() && published == 1;
}

auto worker_competitors() -> bool {
    Source               source;
    std::optional<Reply> output;
    Scheduler            scheduler;
    auto                 task = receive(source, output);
    scheduler.start(task);
    if (scheduler.pump() != 1) {
        return false;
    }

    std::barrier start(3);
    std::thread  first([&] {
        start.arrive_and_wait();
        source.callback(Reason::Received, std::make_unique<int>(11));
    });
    std::thread  second([&] {
        start.arrive_and_wait();
        source.alternate(Reason::Disconnected, std::make_unique<int>(22));
    });
    start.arrive_and_wait();
    first.join();
    second.join();
    if (source.published != 0 || output || task.handle().done() || source.next.try_invoke()) {
        return false;
    }
    if (scheduler.pump() != 1 || task.exception() || !output || !output->value || source.published != 1 || source.consumed != 1 ||
        source.publisher != std::this_thread::get_id()) {
        return false;
    }
    return (output->reason == Reason::Received && *output->value == 11) || (output->reason == Reason::Disconnected && *output->value == 22);
}

auto inline_and_registration_failure() -> bool {
    for (bool throws : {false, true}) {
        Source source;
        source.inline_completion   = true;
        source.registration_throws = throws;
        std::optional<Reply> output;
        Scheduler            scheduler;
        auto                 task = receive(source, output);
        scheduler.start(task);
        const auto resumed = scheduler.pump();
        if (!task.handle().done() || resumed != (throws ? 1U : 2U)) {
            return false;
        }
        if (throws) {
            if (!task.exception() || output || source.published != 0) {
                return false;
            }
        } else if (task.exception() || !output || *output->value != 31 || source.published != 1) {
            return false;
        }
    }
    return true;
}

auto stale_and_canceled() -> bool {
    Source   first, second;
    unsigned phase = 0;
    // Both sources intentionally exercise successive registrations of the same awaitable type.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    auto sequence = [](Source &a, Source &b, unsigned &step) -> gentest::async_test<void> {
        (void)co_await Receive{.source = a, .result = {}};
        step = 1;
        (void)co_await Receive{.source = b, .result = {}};
        step = 2;
    };
    Scheduler scheduler;
    auto      task = sequence(first, second, phase);
    scheduler.start(task);
    if (scheduler.pump() != 1) {
        return false;
    }
    first.callback(Reason::Received, std::make_unique<int>(1));
    if (scheduler.pump() != 1 || phase != 1) {
        return false;
    }
    first.alternate(Reason::Disconnected, std::make_unique<int>(99));
    if (scheduler.pump() != 0 || first.published != 1 || second.published != 0 || phase != 1) {
        return false;
    }

    second.callback(Reason::Received, std::make_unique<int>(2));
    scheduler.cancel_all();
    task = {};
    second.alternate(Reason::Received, std::make_unique<int>(3));
    return scheduler.pump() == 0 && phase == 1 && second.published == 0 && second.stop.stop_requested();
}

auto publication_can_release_owner() -> bool {
    Source               source;
    std::optional<Reply> output;
    Scheduler            scheduler;
    auto                 task = receive(source, output);
    scheduler.start(task);
    if (scheduler.pump() != 1) {
        return false;
    }
    auto frame        = std::weak_ptr(task.frame());
    bool retained     = false;
    source.on_publish = [&] {
        task = {};
        scheduler.cancel_all();
        source.callback  = {};
        source.alternate = {};
        retained         = !frame.expired();
    };
    source.callback(Reason::Received, std::make_unique<int>(7));
    return scheduler.pump() == 0 && retained && frame.expired() && source.published == 1 && !output;
}

auto stop_callback_can_reset_owner() -> bool {
    Source               source;
    std::optional<Reply> output;
    Scheduler            scheduler;
    auto                 task = receive(source, output);
    scheduler.start(task);
    if (scheduler.pump() != 1) {
        return false;
    }
    auto               frame    = std::weak_ptr(task.frame());
    auto               late     = std::move(source.callback);
    bool               retained = false;
    std::stop_callback stop(source.stop, [&] {
        retained = !frame.expired();
        task     = {};
        scheduler.cancel_all();
        source.alternate = {};
    });
    task = {};
    late(Reason::Received, std::make_unique<int>(5));
    return retained && frame.expired() && !output && source.published == 0 && scheduler.pump() == 0;
}

auto rejected_payload_can_reenter_scheduler() -> bool {
    Source               first, second;
    std::optional<Reply> first_output, second_output;
    Scheduler            scheduler;
    auto                 first_task  = receive(first, first_output);
    auto                 second_task = receive(second, second_output);
    scheduler.start(first_task);
    scheduler.start(second_task);
    if (scheduler.pump() != 2) {
        return false;
    }

    // Cancel after the producer enters the control gate, before inbox admission.
    std::latch       entered(1), canceled(1);
    std::atomic_bool first_submission{true};
    scheduler.before_admission = [&] {
        if (first_submission.exchange(false)) {
            entered.count_down();
            canceled.wait();
        }
    };
    bool        accepted = true;
    std::thread worker([&] {
        // Rejected payload destruction submits another live operation.
        std::shared_ptr<void> release(nullptr, [&](void *) noexcept { second.callback(Reason::Received, std::make_unique<int>(81)); });
        accepted = first.next.try_invoke([release = std::move(release)]() noexcept { (void)release; });
    });
    entered.wait();
    first_task = {};
    canceled.count_down();
    worker.join();

    return !accepted && !first_output && second.published == 0 && scheduler.pump() == 1 && !second_task.exception() && second_output &&
           *second_output->value == 81;
}

auto shutdown_and_late_callbacks() -> bool {
    Source               source;
    std::optional<Reply> output;
    auto                 task  = receive(source, output);
    auto                 frame = std::weak_ptr(task.frame());
    {
        Scheduler scheduler;
        scheduler.start(task);
        if (scheduler.pump() != 1) {
            return false;
        }
        source.callback(Reason::Received, std::make_unique<int>(1));
    }
    task = {};
    std::thread worker([&] { source.alternate(Reason::Disconnected, std::make_unique<int>(2)); });
    worker.join();
    return frame.expired() && source.published == 0 && !output && source.stop.stop_requested();
}

auto deadline_arbitration(int admitted_ms, bool timer_first = false, bool already_ready = false) -> bool {
    ManualClock::milliseconds = already_ready ? 200 : 0;
    Source source;
    if (already_ready) {
        source.initial.emplace(Reply{.reason = Reason::Received, .value = std::make_unique<int>(42)});
    }
    std::optional<Reply> output;
    auto                 status = gentest::async::wait_status::ready;
    Scheduler            scheduler;
    auto                 task = timed_receive(source, status, output);
    scheduler.start(task);
    if (scheduler.pump() != 1) {
        return false;
    }
    if (!already_ready) {
        ManualClock::milliseconds = admitted_ms;
        if (timer_first) {
            scheduler.fire_timers();
        }
        std::thread worker([&] { source.callback(Reason::Disconnected, std::make_unique<int>(42)); });
        worker.join();
        if (source.published != 0 || task.handle().done()) {
            return false;
        }
        ManualClock::milliseconds = 300;
        scheduler.fire_timers();
        if (scheduler.pump() != 1) {
            return false;
        }
    }
    if (!task.handle().done() || task.exception()) {
        return false;
    }
    const bool expected_ready = already_ready || (!timer_first && admitted_ms < 100);
    if (expected_ready) {
        return status == gentest::async::wait_status::ready && output && *output->value == 42 && source.consumed == 1 &&
               !source.stop.stop_requested();
    }
    return status == gentest::async::wait_status::timeout && !output && source.published == 0 && source.consumed == 0 &&
           source.canceled == 1 && source.stop.stop_requested();
}

auto reference_arguments_are_owned() -> bool {
    struct TextWait {
        gentest::async::continuation<const std::string &> &callback;
        std::string                                        value;
        auto                                               await_ready() const noexcept -> bool { return false; }
        void await_suspend(std::coroutine_handle<gentest::async_test<std::string>::promise_type> parent) {
            callback = gentest::async::bind_continuation<const std::string &>(
                parent.promise().make_continuation(), [this, owned = std::make_unique<int>(1)](std::string text) noexcept {
                    value = std::move(text);
                    (void)owned;
                });
        }
        auto await_resume() -> std::string { return std::move(value); }
    };
    gentest::async::continuation<const std::string &> callback;
    auto                                              text_task = [](auto &completion) -> gentest::async_test<std::string> {
        co_return co_await TextWait{.callback = completion, .value = {}};
    };
    auto      task = text_task(callback);
    Scheduler scheduler;
    scheduler.start(task);
    if (scheduler.pump() != 1) {
        return false;
    }
    std::string original = "provider stack value";
    callback(original);
    original = "changed before runner delivery";
    return scheduler.pump() == 1 && !task.exception() && task.await_resume() == "provider stack value";
}

auto blocking_external_completion(int cancellation) -> bool {
    Source             source;
    std::promise<void> registered;
    source.registered          = &registered;
    auto                 ready = registered.get_future();
    std::optional<Reply> output;
    std::string          error;
    bool                 completed = false;
    std::thread::id      runner_thread;
    std::thread          runner([&] {
        runner_thread = std::this_thread::get_id();
        completed     = gentest::detail::run_async_task_blocking(receive(source, output), "external completion", error);
    });
    ready.get();
    bool               stop_on_owner = false;
    std::stop_callback stopped(source.stop, [&] {
        auto context  = gentest::detail::current_test();
        stop_on_owner = std::this_thread::get_id() == runner_thread && context && context->display_name == "external completion";
    });
    if (cancellation == 1) {
        source.next.cancel();
    } else if (cancellation == 2) {
        source.callback  = {};
        source.alternate = {};
        source.next      = {};
    } else {
        source.callback(Reason::Received, std::make_unique<int>(64));
    }
    runner.join();
    if (cancellation != 0) {
        return !completed && !error.empty() && !output && source.published == 0 && stop_on_owner;
    }
    return completed && error.empty() && output && *output->value == 64 && source.publisher == runner_thread;
}

Source runner_source;
bool   runner_received          = false;
bool   receiver_context_matches = false;

auto runner_receive() -> gentest::async_test<void> {
    auto      *context       = gentest::detail::current_test().get();
    const auto thread        = std::this_thread::get_id();
    runner_source.on_publish = [context] { receiver_context_matches = gentest::detail::current_test().get() == context; };
    auto result              = co_await gentest::async::wait_for(Receive{.source = runner_source, .result = {}}, 1h);
    ASSERT_TRUE(result.ready());
    auto reply = std::move(result).value();
    EXPECT_EQ(*reply.value, 73);
    EXPECT_TRUE(receiver_context_matches);
    EXPECT_EQ(runner_source.publisher, thread);
    runner_received = true;
}

auto runner_produce() -> gentest::async_test<void> {
    ASSERT_TRUE(static_cast<bool>(runner_source.callback));
    std::thread worker([] { runner_source.callback(Reason::Received, std::make_unique<int>(73)); });
    worker.join();
    EXPECT_EQ(runner_source.published, 0U);
    EXPECT_FALSE(runner_received);
    co_await gentest::async::yield();
    EXPECT_TRUE(runner_received);
}

void unused_sync(void *) {}
auto runner_receive_fn(void *) -> gentest::detail::AsyncTaskPtr { return gentest::detail::make_async_task(runner_receive()); }
auto runner_produce_fn(void *) -> gentest::detail::AsyncTaskPtr { return gentest::detail::make_async_task(runner_produce()); }

const gentest::Case kCases[] = {
    {.name     = "async/completion/00_receive",
     .fn       = &unused_sync,
     .file     = __FILE__,
     .line     = __LINE__,
     .suite    = "async/completion",
     .async_fn = &runner_receive_fn,
     .is_async = true},
    {.name     = "async/completion/01_produce",
     .fn       = &unused_sync,
     .file     = __FILE__,
     .line     = __LINE__,
     .suite    = "async/completion",
     .async_fn = &runner_produce_fn,
     .is_async = true},
};

auto check(bool result, const char *name) -> bool {
    if (!result) {
        (void)std::fprintf(stderr, "%s failed\n", name);
    }
    return result;
}

} // namespace

int main(int argc, char **argv) try {
    if (!check(empty_and_void_callbacks(), "empty and void callbacks") ||
        !check(worker_competitors(), "worker payloads are published once on runner") ||
        !check(inline_and_registration_failure(), "inline and failed registration") ||
        !check(stale_and_canceled(), "stale generations and cancellation") ||
        !check(publication_can_release_owner(), "publication retains frame during owner release") ||
        !check(shutdown_and_late_callbacks(), "shutdown and late callback") ||
        !check(stop_callback_can_reset_owner(), "stop callback can reset the owning task") ||
        !check(rejected_payload_can_reenter_scheduler(), "rejected payload destruction can submit another completion") ||
        !check(deadline_arbitration(90), "on-time admission survives delayed delivery") ||
        !check(deadline_arbitration(100), "admission exactly at deadline times out") ||
        !check(deadline_arbitration(200), "late admission times out without publication") ||
        !check(deadline_arbitration(100, true), "timer wins before callback") ||
        !check(deadline_arbitration(200, false, true), "already-ready value wins expired deadline") ||
        !check(reference_arguments_are_owned(), "queued callback arguments own their values") ||
        !check(blocking_external_completion(0), "blocking fixture waits for an external callback without adoption") ||
        !check(blocking_external_completion(1), "worker cancellation wakes blocking fixture") ||
        !check(blocking_external_completion(2), "retiring the last callback wakes blocking fixture")) {
        return 1;
    }
    gentest::detail::register_cases(std::span<const gentest::Case>(kCases));
    return gentest::run_all_tests(argc, argv);
} catch (const std::exception &error) {
    (void)std::fprintf(stderr, "async completion regression threw: %s\n", error.what());
    return 1;
} catch (...) {
    (void)std::fprintf(stderr, "async completion regression threw an unknown exception\n");
    return 1;
}
