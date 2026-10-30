#include "../../src/async_scheduler_core.h"
#include "gentest/detail/runtime_context.h"
#include "gentest/runner.h"

#include <chrono>
#include <coroutine>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace std::chrono_literals;
using TimePoint = std::chrono::steady_clock::time_point;
using Token     = gentest::detail::AsyncScheduler::WaiterTokenPtr;

struct ManualClock {
    static inline std::chrono::milliseconds::rep milliseconds = 0;
    static auto                                  now() noexcept -> TimePoint { return TimePoint{std::chrono::milliseconds{milliseconds}}; }
};

class Scheduler final : public gentest::detail::AsyncScheduler {
  public:
    Scheduler() : core_({}, &ManualClock::now) { ManualClock::milliseconds = 0; }
    ~Scheduler() override {
        deactivate();
        core_.cancel_all();
    }

    void post(std::coroutine_handle<> handle) override { core_.post(handle); }
    auto frame_for(std::coroutine_handle<> handle) const -> gentest::detail::AsyncFramePtr override { return core_.frame_for(handle); }
    void prepare_transfer(const gentest::detail::AsyncFramePtr &frame) override {
        core_.clear_suspend_state(frame);
        gentest::detail::AsyncScheduler::prepare_transfer(frame);
    }
    void begin_wait(std::coroutine_handle<> handle) noexcept override { core_.begin_wait(handle); }
    void post_frame(const gentest::detail::AsyncFramePtr &frame) override { core_.post(frame); }
    auto post_completion(gentest::detail::AsyncCompletion &&completion) -> bool override {
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
    void pump() {
        gentest::detail::AsyncSchedulerScope scope(this);
        while (auto ready = core_.pop_ready()) {
            (void)ready.resume();
        }
    }
    void cancel_all() { core_.cancel_all(); }
    void advance(std::chrono::milliseconds milliseconds) {
        ManualClock::milliseconds = milliseconds.count();
        for (const auto &[deadline, token] : timers_) {
            if (deadline <= ManualClock::now()) {
                token->post();
            }
        }
    }

  private:
    gentest::runner::AsyncSchedulerCore      core_;
    std::vector<std::pair<TimePoint, Token>> timers_;
};

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename T> void requireDone(gentest::awaitable<T> &task) {
    require(task.handle().done(), "operation did not finish");
    if (task.exception()) {
        std::rethrow_exception(task.exception());
    }
}

struct ImmovableReady {
    explicit ImmovableReady(int value) : value(value) {}
    ImmovableReady(const ImmovableReady &) = delete;
    ImmovableReady(ImmovableReady &&)      = delete;

    auto await_ready() const noexcept -> bool { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    auto await_resume() const noexcept -> int { return value; }

    int value;
};

struct MemberAwaitable {
    unsigned &lookups;
    auto      operator co_await()      &&-> ImmovableReady {
        ++lookups;
        return ImmovableReady{7};
    }
};

struct FreeAwaitable {
    unsigned &lookups;
};
auto operator co_await(FreeAwaitable &&awaitable) -> ImmovableReady {
    ++awaitable.lookups;
    return ImmovableReady{11};
}

struct MoveOnlyReady {
    std::unique_ptr<int> value;
    auto                 await_ready() const noexcept -> bool { return true; }
    void                 await_suspend(std::coroutine_handle<>) const noexcept {}
    auto                 await_resume() -> std::unique_ptr<int> { return std::move(value); }
};

struct MoveReferenceReady {
    std::unique_ptr<int> value;
    auto                 await_ready() const noexcept -> bool { return true; }
    void                 await_suspend(std::coroutine_handle<>) const noexcept {}
    auto                 await_resume() noexcept -> std::unique_ptr<int>                 &&{ return std::move(value); }
};

struct ReferenceReady {
    std::string &value;
    auto         await_ready() const noexcept -> bool { return true; }
    void         await_suspend(std::coroutine_handle<>) const noexcept {}
    auto         await_resume() const noexcept -> std::string         &{ return value; }
};

auto voidValue() -> gentest::awaitable<> { co_return; }

void emptyCompositionIsImmediatelyReady() {
    // An empty join needs neither a scheduler nor a suspension to produce its result.
    auto empty = gentest::async::when_all();
    require(empty.await_ready(), "empty when_all must be immediately ready");

    auto result = empty.await_resume();
    static_assert(std::is_same_v<decltype(result), std::tuple<>>);
    require(result == std::tuple<>{}, "empty when_all must return an empty tuple");
}

void readyFormsAndOwningResults() {
    auto operation = []() -> gentest::awaitable<> {
        unsigned       member_lookups = 0;
        unsigned       free_lookups   = 0;
        ImmovableReady borrowed{3};
        std::string    original = "owned result";
        auto           inputs   = gentest::async::when_all(borrowed, MemberAwaitable{member_lookups}, FreeAwaitable{free_lookups},
                                                           MoveOnlyReady{std::make_unique<int>(19)}, ReferenceReady{original}, voidValue());
        auto           values   = co_await inputs;
        static_assert(std::is_same_v<decltype(values), std::tuple<int, int, int, std::unique_ptr<int>, std::string, std::monostate>>);
        original = "changed after completion";
        require(std::get<0>(values) == 3 && std::get<1>(values) == 7 && std::get<2>(values) == 11, "awaiter lookup lost a value");
        require(*std::get<3>(values) == 19 && std::get<4>(values) == "owned result", "composition results do not own their values");
        require(member_lookups == 1 && free_lookups == 1, "operator co_await evaluated more than once");
        require(borrowed.value == 3, "borrowed immovable operand was changed");

        auto empty = co_await gentest::async::when_all();
        static_assert(std::is_same_v<decltype(empty), std::tuple<>>);
        auto winner = co_await gentest::async::when_any(MoveOnlyReady{std::make_unique<int>(23)}, voidValue());
        require(winner.index() == 0 && *std::get<0>(winner) == 23, "ready winner lost its move-only result");
        auto timed = co_await gentest::async::wait_for(MoveReferenceReady{std::make_unique<int>(31)}, 1h);
        static_assert(std::is_same_v<decltype(timed), gentest::async::wait_result<std::unique_ptr<int>>>);
        require(timed.ready() && *std::move(timed).value() == 31, "wait_for did not own an rvalue-reference result");
    };
    auto      task = operation();
    Scheduler scheduler;
    scheduler.start(task);
    scheduler.pump();
    requireDone(task);
}

struct ResourceReady {
    std::shared_ptr<int> resource;
    auto                 await_ready() const noexcept -> bool { return true; }
    void                 await_suspend(std::coroutine_handle<>) const noexcept {}
    auto                 await_resume() const noexcept -> int { return *resource; }
};

struct SharedResultReady {
    std::shared_ptr<int> resource;
    auto                 await_ready() const noexcept -> bool { return true; }
    void                 await_suspend(std::coroutine_handle<>) const noexcept {}
    auto                 await_resume() noexcept -> std::shared_ptr<int> { return std::move(resource); }
};

struct ThrowOnSecondMove {
    explicit ThrowOnSecondMove(unsigned &count) : moves(&count) {}
    ThrowOnSecondMove(const ThrowOnSecondMove &) = delete;
    // Deliberately fail result extraction to verify cleanup of joined payloads.
    // NOLINTNEXTLINE(bugprone-exception-escape,performance-noexcept-move-constructor)
    ThrowOnSecondMove(ThrowOnSecondMove &&other) : moves(other.moves) {
        if (++*moves == 2) {
            throw std::runtime_error("selected result move");
        }
    }
    unsigned *moves;
};

struct ThrowingResultReady {
    unsigned &moves;
    auto      await_ready() const noexcept -> bool { return true; }
    void      await_suspend(std::coroutine_handle<>) const noexcept {}
    auto      await_resume() const -> ThrowOnSecondMove { return ThrowOnSecondMove{moves}; }
};

void namedCompositionReleasesInputs() {
    auto operation = []() -> gentest::awaitable<> {
        auto               resource = std::make_shared<int>(37);
        std::weak_ptr<int> lifetime = resource;
        auto               named    = gentest::async::when_any(ResourceReady{std::move(resource)}, voidValue());
        require(!lifetime.expired(), "named composition did not own its rvalue input");
        auto winner = co_await named;
        require(winner.index() == 0 && std::get<0>(winner) == 37, "named composition lost its result");
        require(lifetime.expired(), "named composition retained completed awaiter resources");
        co_await gentest::async::yield();
        require(lifetime.expired(), "completed input resources survived unrelated suspension");

        auto               losing_resource = std::make_shared<int>(41);
        std::weak_ptr<int> losing_lifetime = losing_resource;
        auto               second_race     = gentest::async::when_any(voidValue(), SharedResultReady{std::move(losing_resource)});
        auto               second_winner   = co_await second_race;
        require(second_winner.index() == 0, "ready void awaitable failed to win its race");
        require(losing_lifetime.expired(), "named composition retained a losing result payload after joining");

        unsigned           moves             = 0;
        auto               throwing_loser    = std::make_shared<int>(43);
        std::weak_ptr<int> throwing_lifetime = throwing_loser;
        auto throwing_race = gentest::async::when_any(ThrowingResultReady{moves}, SharedResultReady{std::move(throwing_loser)});
        bool caught        = false;
        try {
            (void)co_await throwing_race;
        } catch (const std::runtime_error &error) { caught = std::string_view(error.what()) == "selected result move"; }
        require(caught && moves == 2, "selected result extraction did not exercise the throwing move");
        require(throwing_lifetime.expired(), "throwing result extraction retained a joined loser payload");
    };
    auto      task = operation();
    Scheduler scheduler;
    scheduler.start(task);
    scheduler.pump();
    requireDone(task);
}

struct Source {
    gentest::async::continuation_handle next;
    std::function<bool(int)>            callback;
    unsigned                            started   = 0;
    unsigned                            published = 0;
    unsigned                            consumed  = 0;
};

enum class Suspension { Void, Bool, Handle };

template <Suspension Form> struct Receive {
    Source &source;
    int     value = 0;

    auto                             await_ready() const noexcept -> bool { return false; }
    template <typename Promise> auto await_suspend(std::coroutine_handle<Promise> parent) {
        ++source.started;
        source.next     = parent.promise().make_continuation();
        source.callback = gentest::async::bind_continuation<int>(source.next, [this](int result) noexcept {
            value = result;
            ++source.published;
        });
        if constexpr (Form == Suspension::Bool) {
            return true;
        } else if constexpr (Form == Suspension::Handle) {
            return std::noop_coroutine();
        }
    }
    auto await_resume() -> int {
        ++source.consumed;
        return value;
    }
};

struct FalseWait {
    auto await_ready() const noexcept -> bool { return false; }
    auto await_suspend(std::coroutine_handle<>) const noexcept -> bool { return false; }
    auto await_resume() const noexcept -> int { return 29; }
};

void suspensionFormsAndOrderedJoin() {
    Source                                        first, second, third;
    std::optional<std::tuple<int, int, int, int>> output;
    auto                                          operation = [](Source &a, Source &b, Source &c, auto &result) -> gentest::awaitable<> {
        result = co_await gentest::async::when_all(Receive<Suspension::Void>{.source = a}, Receive<Suspension::Bool>{.source = b},
                                                   Receive<Suspension::Handle>{.source = c}, FalseWait{});
    };
    auto      task = operation(first, second, third, output);
    Scheduler scheduler;
    scheduler.start(task);
    scheduler.pump();
    require(first.started == 1 && second.started == 1 && third.started == 1, "when_all did not start every input");
    require(!task.handle().done() && !output, "when_all finished before its inputs");

    require(third.callback(31) && second.callback(17), "ordinary completion was rejected");
    scheduler.pump();
    require(!task.handle().done() && !output, "when_all failed to join its first input");
    require(first.callback(5), "first input completion was rejected");
    scheduler.pump();
    requireDone(task);
    require(output == std::tuple{5, 17, 31, 29}, "when_all changed argument order");
    require(!first.callback(99) && !second.callback(99) && !third.callback(99), "completed inputs retained live callbacks");
    scheduler.pump();
    require(first.published == 1 && second.published == 1 && third.published == 1, "late callback republished into finished storage");
}

enum class OwnerRelease { Resume, Publish, FalseSuspend, SelfTransfer, ThrowSuspend };

template <OwnerRelease Mode> struct ReleaseGroupOwner {
    Source                &source;
    std::shared_ptr<int>   resource;
    std::function<void()> &release;
    bool                  &consumed;

    auto                             await_ready() const noexcept -> bool { return false; }
    template <typename Promise> auto await_suspend(std::coroutine_handle<Promise> parent) {
        ++source.started;
        if constexpr (Mode == OwnerRelease::Resume || Mode == OwnerRelease::Publish) {
            source.next     = parent.promise().make_continuation();
            source.callback = gentest::async::bind_continuation<int>(source.next, [this](int value) noexcept {
                if constexpr (Mode == OwnerRelease::Publish) {
                    release();
                }
                *resource = value;
                ++source.published;
            });
            return true;
        } else {
            release();
            *resource = 53;
            if constexpr (Mode == OwnerRelease::FalseSuspend) {
                return false;
            } else if constexpr (Mode == OwnerRelease::SelfTransfer) {
                return parent;
            } else {
                throw std::runtime_error("registration failed after releasing group owner");
            }
        }
    }
    auto await_resume() -> int {
        if constexpr (Mode == OwnerRelease::Resume) {
            release();
        }
        consumed = true;
        return *resource;
    }
};

template <OwnerRelease Mode> void groupOwnerReleaseCase(bool abandon_pending = false) {
    Source               source;
    Scheduler            scheduler;
    gentest::awaitable<> parent;
    auto                 resource            = std::make_shared<int>(0);
    std::weak_ptr<int>   lifetime            = resource;
    bool                 retained_in_release = false;
    struct Outcome {
        bool consumed        = false;
        bool parent_returned = false;
    } outcome;
    std::function<void()> release = [&] {
        parent = {};
        scheduler.cancel_all();
        retained_in_release = !lifetime.expired();
    };
    auto operation = [](Source &input, std::shared_ptr<int> hold, std::function<void()> &on_resume,
                        Outcome &progress) -> gentest::awaitable<> {
        auto group = gentest::async::when_all(
            ReleaseGroupOwner<Mode>{.source = input, .resource = std::move(hold), .release = on_resume, .consumed = progress.consumed});
        (void)co_await group;
        progress.parent_returned = true;
    };
    parent                                           = operation(source, std::move(resource), release, outcome);
    std::weak_ptr<gentest::detail::AsyncFrame> frame = parent.frame();
    scheduler.start(parent);
    scheduler.pump();
    require(source.started == 1, "group release fixture did not start its input");

    if constexpr (Mode == OwnerRelease::Resume || Mode == OwnerRelease::Publish) {
        require(!parent.handle().done(), "group release fixture did not suspend");
        if (abandon_pending) {
            parent = {};
            scheduler.cancel_all();
            require(!outcome.consumed && frame.expired() && lifetime.expired(), "abandoned suspended group retained an ownership cycle");
            require(!source.callback(59), "abandoned suspended group retained a live callback");
            return;
        }
        require(source.callback(53), "group input rejected its first result");
        scheduler.pump();
        require(!source.callback(59), "canceled group retained a live late callback");
    }

    require(retained_in_release, "group input storage died inside its registration or extraction callback");
    require(outcome.consumed == (Mode != OwnerRelease::ThrowSuspend && Mode != OwnerRelease::Publish),
            "group input did not finish its expected extraction path");
    require(!outcome.parent_returned && frame.expired() && lifetime.expired(),
            "canceled group resumed its parent or retained state after extraction");
}

void releasingGroupOwnerDuringExtraction() {
    groupOwnerReleaseCase<OwnerRelease::Resume>();
    groupOwnerReleaseCase<OwnerRelease::Publish>();
    groupOwnerReleaseCase<OwnerRelease::FalseSuspend>();
    groupOwnerReleaseCase<OwnerRelease::SelfTransfer>();
    groupOwnerReleaseCase<OwnerRelease::ThrowSuspend>();
    groupOwnerReleaseCase<OwnerRelease::Resume>(true);
}

struct LiveParameter {
    explicit LiveParameter(unsigned &live) : live(live) { ++live; }
    ~LiveParameter() { --live; }
    unsigned &live;
};

auto synchronousChild(std::unique_ptr<LiveParameter> parameter) -> gentest::awaitable<> {
    (void)parameter;
    co_return;
}

auto deepChild(unsigned depth) -> gentest::awaitable<unsigned> {
    if (depth == 0) {
        co_await gentest::async::yield();
        co_return 0;
    }
    co_return 1 + co_await deepChild(depth - 1);
}

void nativeTransferDepthAndSiblingRetirement() {
    auto operation = [](unsigned &live) -> gentest::awaitable<> {
        // Parameter destruction requires frame destruction, so this detects
        // execution guards or scheduler registrations retaining finished siblings.
        for (unsigned index = 0; index != 512; ++index) {
            co_await synchronousChild(std::make_unique<LiveParameter>(live));
            require(live == 0, "completed synchronous child frame survived its co_await expression");
        }
        require(co_await deepChild(16384) == 16384, "deep native transfer chain lost a result");
    };
    unsigned  live = 0;
    auto      task = operation(live);
    Scheduler scheduler;
    scheduler.start(task);
    scheduler.pump();
    requireDone(task);
    require(live == 0, "native transfer test retained a coroutine parameter");
}

auto throwAfter(gentest::async::event<int> &gate, const char *message) -> gentest::awaitable<int> {
    (void)co_await gate.wait("exception gate");
    throw std::runtime_error(message);
}

void allJoinsBeforeFirstException() {
    gentest::async::event<int> first, second;
    std::string                caught;
    auto                       operation = [](auto &a, auto &b, std::string &message) -> gentest::awaitable<> {
        try {
            (void)co_await gentest::async::when_all(throwAfter(a, "first input"), throwAfter(b, "first completion"));
        } catch (const std::runtime_error &error) { message = error.what(); }
    };
    auto      task = operation(first, second, caught);
    Scheduler scheduler;
    scheduler.start(task);
    scheduler.pump();
    second.set("exception gate", 1);
    scheduler.pump();
    require(!task.handle().done() && caught.empty(), "when_all propagated an error before joining every input");
    first.set("exception gate", 1);
    scheduler.pump();
    requireDone(task);
    require(caught == "first completion", "when_all changed the first observed exception");
}

struct Cleanup {
    gentest::async::event<int> work;
    gentest::async::event<int> finish;
    bool                       stopped = false;
    bool                       cleaned = false;
};

auto cancellableChild(Cleanup &state) -> gentest::awaitable<int> {
    try {
        (void)co_await state.work.wait("pending work");
        throw std::runtime_error("canceled work completed normally");
    } catch (const gentest::async::operation_cancelled &) { state.stopped = true; }

    // Cleanup must survive the inherited stop request, and the parent must join it.
    (void)co_await state.finish.wait("cleanup", std::stop_token{});
    state.cleaned = true;
    co_return 101;
}

void anyCancelsAndJoinsWithoutChangingWinner() {
    Source                                first;
    Cleanup                               loser;
    std::optional<std::variant<int, int>> output;
    auto                                  operation = [](Source &winner, Cleanup &other, auto &result) -> gentest::awaitable<> {
        result = co_await gentest::async::when_any(Receive<Suspension::Void>{.source = winner}, cancellableChild(other));
    };
    auto      task = operation(first, loser, output);
    Scheduler scheduler;
    scheduler.start(task);
    scheduler.pump();
    require(first.started == 1 && !loser.stopped, "race did not start its operands");
    require(first.callback(47), "winner completion was rejected");
    scheduler.pump();
    require(loser.stopped && !loser.cleaned && !task.handle().done() && !output, "when_any did not cancel and join loser cleanup");
    loser.finish.set("cleanup", 1);
    scheduler.pump();
    requireDone(task);
    require(loser.cleaned && output && output->index() == 0 && std::get<0>(*output) == 47, "loser cleanup replaced the winner");
    require(!first.callback(91), "race winner callback remained active after join");
}

void anyKeepsFirstExceptionThroughCleanup() {
    gentest::async::event<int> fail;
    Cleanup                    loser;
    std::string                caught;
    auto                       operation = [](auto &failure, Cleanup &other, std::string &message) -> gentest::awaitable<> {
        try {
            (void)co_await gentest::async::when_any(throwAfter(failure, "race failure"), cancellableChild(other));
        } catch (const std::runtime_error &error) { message = error.what(); }
    };
    auto      task = operation(fail, loser, caught);
    Scheduler scheduler;
    scheduler.start(task);
    scheduler.pump();
    fail.set("exception gate", 1);
    scheduler.pump();
    require(loser.stopped && caught.empty() && !task.handle().done(), "race exception escaped before loser cleanup");
    loser.finish.set("cleanup", 1);
    scheduler.pump();
    requireDone(task);
    require(loser.cleaned && caught == "race failure", "race lost the winning exception");
}

void timeoutCancelsAndJoins() {
    Cleanup                                    child;
    std::optional<gentest::async::wait_status> status;
    auto                                       operation = [](Cleanup &state, auto &outcome) -> gentest::awaitable<> {
        auto result = co_await gentest::async::timeout_detail::timeout_awaitable<gentest::awaitable<int>, ManualClock>{
            cancellableChild(state), TimePoint{100ms}, std::source_location::current()};
        outcome = result.status();
    };
    auto      task = operation(child, status);
    Scheduler scheduler;
    scheduler.start(task);
    scheduler.pump();
    scheduler.advance(100ms);
    scheduler.pump();
    require(child.stopped && !child.cleaned && !task.handle().done() && !status, "timeout did not wait for child cleanup");
    child.finish.set("cleanup", 1);
    scheduler.pump();
    requireDone(task);
    require(child.cleaned && status == gentest::async::wait_status::timeout, "late child result replaced timeout");
}

void directFutureAndExpiredDeadline() {
    auto operation = []() -> gentest::awaitable<> {
        gentest::async::promise<std::unique_ptr<int>> ready;
        ready.set_value(std::make_unique<int>(61));
        auto result = co_await gentest::async::wait_for(ready.get_future(), -1ms);
        require(result.ready() && *std::move(result).value() == 61, "ready direct future lost to an expired deadline");

        // Expired pending inputs are never subscribed and remain available.
        gentest::async::promise<int> promise;
        auto                         future  = promise.get_future();
        auto                         timeout = co_await gentest::async::wait_for(future, -1ms);
        require(timeout.timed_out(), "pending direct future ignored an expired deadline");
        promise.set_value(67);
        require(co_await future == 67, "timed-out future consumed its producer value");

        Source source;
        auto   expired = co_await gentest::async::wait_for(Receive<Suspension::Bool>{.source = source}, -1ms);
        require(expired.timed_out() && source.started == 0, "expired ordinary awaiter was registered");
    };
    auto      task = operation();
    Scheduler scheduler;
    scheduler.start(task);
    scheduler.pump();
    requireDone(task);
}

void ordinaryCompletionDeadlineReceipt() {
    for (const auto received : {90ms, 100ms, 200ms}) {
        Source                                          source;
        std::optional<gentest::async::wait_result<int>> output;
        auto                                            operation = [](Source &input, auto &result) -> gentest::awaitable<> {
            result = co_await gentest::async::timeout_detail::timeout_awaitable<Receive<Suspension::Void>, ManualClock>{
                Receive<Suspension::Void>{.source = input}, TimePoint{100ms}, std::source_location::current()};
        };
        auto      task = operation(source, output);
        Scheduler scheduler;
        scheduler.start(task);
        scheduler.pump();
        require(source.started == 1 && !task.handle().done(), "ordinary timeout input did not register");

        // Admission time decides the result even when delivery and timer handling are delayed.
        ManualClock::milliseconds = received.count();
        require(source.callback(79), "ordinary timeout input rejected its first completion");
        scheduler.advance(300ms);
        scheduler.pump();
        requireDone(task);
        if (!output) {
            throw std::runtime_error("ordinary timeout failed to produce an outcome");
        }
        require(output->ready() == (received < 100ms), "ordinary timeout used delivery time or accepted the deadline boundary");
        if (output->ready()) {
            require(output->value() == 79, "on-time ordinary completion lost its value");
        }
        require(source.published == 1 && source.consumed == 1, "timeout did not join admitted ordinary completion");
        require(!source.callback(83), "timeout left a live ordinary callback after joining");
    }
}

void pendingFutureTimeoutPreservesValue() {
    gentest::async::promise<int>               producer;
    auto                                       future = producer.get_future();
    std::optional<gentest::async::wait_status> status;
    auto                                       operation = [](auto &input, auto &outcome) -> gentest::awaitable<> {
        auto result = co_await gentest::async::timeout_detail::timeout_awaitable<decltype((input)), ManualClock>{
            input, TimePoint{100ms}, std::source_location::current()};
        outcome = result.status();
    };
    auto      task = operation(future, status);
    Scheduler scheduler;
    scheduler.start(task);
    scheduler.pump();
    require(!task.handle().done(), "future timeout did not suspend");
    scheduler.advance(100ms);
    scheduler.pump();
    requireDone(task);
    require(status == gentest::async::wait_status::timeout, "pending future did not time out");
    producer.set_value(71);
    auto consume = [](auto &input) -> gentest::awaitable<int> { co_return co_await input; };
    auto second  = consume(future);
    scheduler.start(second);
    scheduler.pump();
    requireDone(second);
    require(second.await_resume() == 71, "canceled future waiter consumed a later producer value");
}

void inheritedStopPrecedesReadyBuiltins() {
    auto operation = []() -> gentest::awaitable<> {
        gentest::async::event<int> event;
        event.set("ready", 89);
        bool event_canceled = false;
        try {
            (void)co_await event.wait("ready");
        } catch (const gentest::async::operation_cancelled &) { event_canceled = true; }
        require(event_canceled, "ready event ignored inherited cancellation");
        require(co_await event.wait("ready", std::stop_token{}) == 89, "empty stop override did not preserve ready event value");

        gentest::async::promise<int> promise;
        auto                         future = promise.get_future();
        promise.set_value(97);
        bool future_canceled = false;
        try {
            (void)co_await future;
        } catch (const gentest::async::operation_cancelled &) { future_canceled = true; }
        require(future_canceled, "ready future ignored inherited cancellation");
        require(co_await future.wait(std::stop_token{}) == 97, "empty stop override did not preserve ready future value");

        bool sleep_canceled = false;
        try {
            co_await gentest::async::sleep_for(-1ms);
        } catch (const gentest::async::operation_cancelled &) { sleep_canceled = true; }
        require(sleep_canceled, "elapsed sleep ignored inherited cancellation");
        co_await gentest::async::sleep_for(-1ms, std::stop_token{});

        bool deadline_canceled = false;
        try {
            co_await gentest::async::sleep_until(TimePoint{});
        } catch (const gentest::async::operation_cancelled &) { deadline_canceled = true; }
        require(deadline_canceled, "expired sleep deadline ignored inherited cancellation");
        co_await gentest::async::sleep_until(TimePoint{}, std::stop_token{});
        co_await gentest::async::yield(std::stop_token{});
    };
    std::stop_source stop;
    stop.request_stop();
    auto task = operation();
    task.frame()->set_stop_token(stop.get_token());
    Scheduler scheduler;
    scheduler.start(task);
    scheduler.pump();
    requireDone(task);
}

auto contextChild(gentest::detail::TestContextInfo *expected) -> gentest::awaitable<int> {
    require(gentest::detail::current_test().get() == expected, "composition child started outside owner context");
    co_await gentest::async::yield();
    require(gentest::detail::current_test().get() == expected, "composition child resumed outside owner context");
    co_return 1;
}

void compositionKeepsContext() {
    auto operation = []() -> gentest::awaitable<> {
        auto *context = gentest::detail::current_test().get();
        require(context != nullptr, "blocking runner did not install a context");
        auto all = co_await gentest::async::when_all(contextChild(context), contextChild(context));
        require(std::get<0>(all) + std::get<1>(all) == 2, "context children lost results");
        require(gentest::detail::current_test().get() == context, "composition parent lost owner context");
    };
    std::string error;
    require(gentest::detail::run_async_task_blocking(operation(), "composition context", error), error.c_str());
}

} // namespace

int main() {
    struct Test {
        const char *name;
        void (*run)();
    };
    const Test tests[] = {
        {.name = "empty composition is immediately ready", .run = &emptyCompositionIsImmediatelyReady},
        {.name = "ready forms and owning results", .run = &readyFormsAndOwningResults},
        {.name = "named composition releases inputs", .run = &namedCompositionReleasesInputs},
        {.name = "ordinary suspension forms and ordered join", .run = &suspensionFormsAndOrderedJoin},
        {.name = "group owner release during result extraction", .run = &releasingGroupOwnerDuringExtraction},
        {.name = "native transfer depth and completed sibling retirement", .run = &nativeTransferDepthAndSiblingRetirement},
        {.name = "when_all joins before first exception", .run = &allJoinsBeforeFirstException},
        {.name = "when_any cancels, joins, and preserves winner", .run = &anyCancelsAndJoinsWithoutChangingWinner},
        {.name = "when_any preserves first exception through cleanup", .run = &anyKeepsFirstExceptionThroughCleanup},
        {.name = "timeout cancels and joins", .run = &timeoutCancelsAndJoins},
        {.name = "direct future and expired deadline", .run = &directFutureAndExpiredDeadline},
        {.name = "ordinary completion deadline receipt", .run = &ordinaryCompletionDeadlineReceipt},
        {.name = "future timeout preserves later value", .run = &pendingFutureTimeoutPreservesValue},
        {.name = "ready builtins honor inherited stop and empty override", .run = &inheritedStopPrecedesReadyBuiltins},
        {.name = "composition keeps owning test context", .run = &compositionKeepsContext},
    };
    for (const auto &test : tests) {
        try {
            test.run();
        } catch (const std::exception &error) {
            (void)std::fprintf(stderr, "%s: %s\n", test.name, error.what());
            return 1;
        }
    }
    (void)std::printf("async composition: %zu checks passed\n", std::size(tests));
    return 0;
}
