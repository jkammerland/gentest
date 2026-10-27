#include "gentest/bench_util.h"
#include "gentest/benchmark.h"
#include "gentest/detail/generated_runtime.h"
#include "gentest/detail/runtime_context.h"
#include "gentest/framework_events.h"
#include "gentest/runner.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <functional>
#include <latch>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
using gentest::NotificationKind;
using gentest::NotifyResult;
using gentest::SuspensionKind;

void check(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Record {
    NotificationKind      kind = NotificationKind::custom;
    std::uint64_t         subject{};
    std::array<char, 128> text{};
};

struct CaptureSink final : gentest::NotificationSink {
    // Each record's member initializers provide valid defaults for unused slots.
    std::array<Record, 1024> records;
    std::atomic<std::size_t> size{0};
    bool                     failProgress = false;

    auto submit(const gentest::Notification &notification) noexcept -> NotifyResult override {
        if (failProgress && notification.kind == NotificationKind::progress) {
            return NotifyResult::queue_overflow;
        }
        const auto index = size.fetch_add(1, std::memory_order_relaxed);
        if (index >= records.size()) {
            return NotifyResult::queue_overflow;
        }
        auto &record     = records[index];
        record.kind      = notification.kind;
        record.subject   = notification.subject;
        const auto count = std::min(notification.payload.size(), record.text.size() - 1);
        std::ranges::copy(notification.payload.substr(0, count), record.text.begin());
        return NotifyResult::accepted;
    }

    auto count(NotificationKind kind) const -> std::size_t {
        return static_cast<std::size_t>(
            std::count_if(records.begin(), records.begin() + size.load(), [kind](const Record &record) { return record.kind == kind; }));
    }
};

struct Visitor final : gentest::FrameworkEventVisitor {
    std::array<std::size_t, 7>                            suspensions{};
    std::vector<std::uint64_t>                            subjects;
    std::vector<std::pair<SuspensionKind, std::uint64_t>> suspensionSubjects;
    std::function<void(SuspensionKind)>                   nextAction;
    unsigned                                              starts                    = 0;
    unsigned                                              finishes                  = 0;
    unsigned                                              runStarts                 = 0;
    unsigned                                              runFinishes               = 0;
    unsigned                                              fixtureStarts             = 0;
    unsigned                                              fixtureFinishes           = 0;
    unsigned                                              phaseStarts               = 0;
    unsigned                                              phaseFinishes             = 0;
    bool                                                  filter                    = false;
    bool                                                  callbackInsideMeasurement = false;
    bool                                                 *insideMeasurement         = nullptr;

    Visitor() {
        subjects.reserve(1024);
        suspensionSubjects.reserve(256);
    }
    void onRunStart(const gentest::FrameworkEvent &) noexcept override { ++runStarts; }
    void onRunFinish(const gentest::FrameworkEvent &) noexcept override { ++runFinishes; }
    void onCaseStart(const gentest::FrameworkEvent &event) noexcept override {
        ++starts;
        subjects.push_back(event.notifier.subject());
    }
    void onCaseFinish(const gentest::FrameworkEvent &) noexcept override { ++finishes; }
    void onFixtureStart(const gentest::FrameworkEvent &event) noexcept override {
        ++fixtureStarts;
        FrameworkEventVisitor::onFixtureStart(event);
    }
    void onFixtureFinish(const gentest::FrameworkEvent &event) noexcept override {
        ++fixtureFinishes;
        FrameworkEventVisitor::onFixtureFinish(event);
    }
    void onPhaseStart(const gentest::FrameworkEvent &event) noexcept override {
        ++phaseStarts;
        callbackInsideMeasurement |= insideMeasurement && *insideMeasurement;
        FrameworkEventVisitor::onPhaseStart(event);
    }
    void onPhaseFinish(const gentest::FrameworkEvent &event) noexcept override {
        ++phaseFinishes;
        callbackInsideMeasurement |= insideMeasurement && *insideMeasurement;
        FrameworkEventVisitor::onPhaseFinish(event);
    }
    void onSuspend(const gentest::FrameworkEvent &event) noexcept override {
        ++suspensions[static_cast<std::size_t>(event.suspension)];
        suspensionSubjects.emplace_back(event.suspension, event.notifier.subject());
        if (auto action = std::move(nextAction)) {
            action(event.suspension);
        }
        if (!filter) {
            FrameworkEventVisitor::onSuspend(event);
        }
    }
};

Visitor *visitor = nullptr;

struct CustomAwaiter {
    bool                             throws = false;
    bool                             await_ready() const noexcept { return false; }
    template <typename Promise> bool await_suspend(std::coroutine_handle<Promise> handle) const {
        auto next = handle.promise().make_continuation();
        if (throws) {
            throw std::runtime_error("registration failed");
        }
        return false;
    }
    void await_resume() const noexcept {}
};

auto child() -> gentest::awaitable<> { co_await gentest::async::yield(); }

struct DeadlineRaceClock {
    static inline unsigned reads = 0;
    static auto            now() -> std::chrono::steady_clock::time_point {
        return std::chrono::steady_clock::time_point{} + (++reads == 1 ? 0ms : 20ms);
    }
};

auto allPaths() -> gentest::awaitable<> {
    co_await gentest::async::yield();

    // Cancellation at the source hook makes the timer boundary deterministic.
    std::stop_source stop;
    visitor->nextAction = [&](SuspensionKind kind) {
        check(kind == SuspensionKind::timer, "timer source boundary missing");
        stop.request_stop();
    };
    bool canceled = false;
    try {
        co_await gentest::async::sleep_for(1h, stop.get_token());
    } catch (const gentest::async::operation_cancelled &) { canceled = true; }
    check(canceled, "timer cancellation lost");

    // Publishing inside the hook proves it runs before the awaiter's lock and
    // exercises the ready race between await_ready and registration.
    gentest::async::event<int> event;
    visitor->nextAction = [&](SuspensionKind kind) {
        check(kind == SuspensionKind::event, "event boundary missing");
        event.set("key", 7);
    };
    check(co_await event.wait("key") == 7, "event ready race lost value");

    gentest::async::promise<int> promise;
    auto                         future = promise.get_future();
    visitor->nextAction                 = [&](SuspensionKind kind) {
        check(kind == SuspensionKind::future, "value future boundary missing");
        promise.set_value(8);
    };
    check(co_await future == 8, "future ready race lost value");

    gentest::async::promise<void> voidPromise;
    auto                          voidFuture = voidPromise.get_future();
    visitor->nextAction                      = [&](SuspensionKind kind) {
        check(kind == SuspensionKind::future, "void future boundary missing");
        voidPromise.set_value();
    };
    co_await voidFuture;

    co_await CustomAwaiter{};
    bool threw = false;
    try {
        co_await CustomAwaiter{true};
    } catch (const std::runtime_error &) { threw = true; }
    check(threw, "custom await_suspend throw lost");

    co_await child();
    co_await gentest::async::when_all(child(), child());

    DeadlineRaceClock::reads = 0;
    auto raced               = co_await gentest::async::timeout_detail::timeout_awaitable<gentest::awaitable<>, DeadlineRaceClock>{
        child(), std::chrono::steady_clock::time_point{} + 10ms, std::source_location::current()};
    check(raced.timed_out(), "composition deadline race did not return false");

    // Readiness avoids await_suspend and must not publish another source event.
    const auto before = visitor->suspensions;
    co_await event.wait("key");
    co_await gentest::async::sleep_until(std::chrono::steady_clock::time_point::min());
    gentest::async::promise<int> readyPromise;
    auto                         readyFuture = readyPromise.get_future();
    readyPromise.set_value(9);
    check(co_await readyFuture == 9, "ready future value changed");
    check(visitor->suspensions == before, "ready operation emitted suspension");
}

auto allPathsAdapter(void *) -> gentest::detail::AsyncTaskPtr { return gentest::detail::make_async_task(allPaths()); }
auto childAdapter(void *) -> gentest::detail::AsyncTaskPtr { return gentest::detail::make_async_task(child()); }

auto makeCase(std::string_view name, void (*body)(void *) = nullptr) -> gentest::Case {
    return {.name             = name,
            .fn               = body,
            .file             = __FILE__,
            .line             = __LINE__,
            .should_skip      = false,
            .fixture_lifetime = gentest::FixtureLifetime::None,
            .suite            = "notifications"};
}

auto run(std::span<const gentest::Case> cases, const std::shared_ptr<CaptureSink> &sink, const std::shared_ptr<Visitor> &hooks = {},
         std::span<const char *> arguments = {}) -> int {
    const char               *defaults[] = {"framework-events", "--no-color"};
    gentest::RunConfiguration configuration{.notifier = gentest::Notifier(sink, {.id = 71, .generation = 3}, 0), .frameworkEvents = hooks};
    visitor = hooks.get();
    return gentest::run_cases(cases, arguments.empty() ? std::span<const char *>(defaults) : arguments, configuration);
}

void suspensionBoundariesAndFiltering() {
    auto test     = makeCase("notifications/all_paths");
    test.async_fn = &allPathsAdapter;
    test.is_async = true;
    for (const bool filter : {false, true}) {
        auto sink     = std::make_shared<CaptureSink>();
        auto hooks    = std::make_shared<Visitor>();
        hooks->filter = filter;
        check(run({&test, 1}, sink, hooks) == 0, "all suspension paths failed");
        check(hooks->suspensions[static_cast<std::size_t>(SuspensionKind::yield)] == 4, "yield boundary count");
        check(hooks->suspensions[static_cast<std::size_t>(SuspensionKind::timer)] == 1, "timer boundary count");
        check(hooks->suspensions[static_cast<std::size_t>(SuspensionKind::event)] == 1, "event boundary count");
        check(hooks->suspensions[static_cast<std::size_t>(SuspensionKind::future)] == 2, "both future boundaries");
        check(hooks->suspensions[static_cast<std::size_t>(SuspensionKind::continuation)] == 2, "false/throw continuation boundary");
        check(hooks->suspensions[static_cast<std::size_t>(SuspensionKind::composition)] == 2, "composition boundary count");
        check(hooks->suspensions[static_cast<std::size_t>(SuspensionKind::native_child)] == 3, "native child boundary count");
        check(sink->count(NotificationKind::progress) == (filter ? 0 : 15), "custom filtering changed lifecycle or source semantics");
        check(sink->count(NotificationKind::silence_allowance) == 0, "suspension silently granted an allowance");
        check(sink->count(NotificationKind::subject_started) == 1 && sink->count(NotificationKind::subject_finished) == 1,
              "case lifecycle missing");
        check(hooks->runStarts == 1 && hooks->runFinishes == 1 && hooks->starts == 1 && hooks->finishes == 1, "visitor lifecycle missing");
    }
}

void disabledAndVisitorOnlyObservation() {
    auto test = makeCase(
        "notifications/disabled", +[](void *) { check(!gentest::currentNotifier().active(), "disabled observation installed a sink"); });
    const char *arguments[] = {"framework-events", "--no-color"};
    check(gentest::run_cases({&test, 1}, arguments) == 0, "disabled run failed");

    auto                            hooks = std::make_shared<Visitor>();
    const gentest::RunConfiguration configuration{.frameworkEvents = hooks};
    check(gentest::run_cases({&test, 1}, arguments, configuration) == 0, "visitor-only run failed");
    check(hooks->subjects.size() == 1 && hooks->subjects.front() != 0, "visitor-only subject identity missing");
}

void defaultVisitorAndLogs() {
    auto sink     = std::make_shared<CaptureSink>();
    auto test     = makeCase("notifications/default");
    test.async_fn = &childAdapter;
    test.is_async = true;
    check(run({&test, 1}, sink) == 0, "default visitor failed");
    check(sink->count(NotificationKind::progress) == 1, "default onSuspend did not renew progress");

    sink = std::make_shared<CaptureSink>();
    test = makeCase("notifications/log", +[](void *) { gentest::log("ordinary log is not progress"); });
    check(run({&test, 1}, sink) == 0, "log case failed");
    check(sink->count(NotificationKind::progress) == 0, "logging renewed progress");
}

gentest::async::event<int> siblingEvent;
gentest::CurrentContext    siblingContext;

auto waitingSibling() -> gentest::awaitable<> {
    siblingContext = gentest::get_current_context();
    check(co_await siblingEvent.wait("release") == 1, "sibling event lost value");
}

auto releasingSibling() -> gentest::awaitable<> {
    {
        // Borrowed producer TLS is deliberately a different case. Suspension
        // attribution must still come from this coroutine's registered frame.
        auto borrowed = gentest::set_current_context(siblingContext);
        co_await gentest::async::yield();
    }
    co_await gentest::async::yield();
    siblingEvent.set("release", 1);
}

void concurrentSubjectsAndFrameOwnership() {
    auto       sink  = std::make_shared<CaptureSink>();
    auto       hooks = std::make_shared<Visitor>();
    std::array cases{makeCase("notifications/a_wait"), makeCase("notifications/z_release")};
    cases[0].async_fn = +[](void *) { return gentest::detail::make_async_task(waitingSibling()); };
    cases[1].async_fn = +[](void *) { return gentest::detail::make_async_task(releasingSibling()); };
    check(run(cases, sink, hooks) == 0, "concurrent cases failed");
    check(hooks->subjects.size() == 2 && hooks->subjects[0] != hooks->subjects[1], "siblings shared one subject");
    check(hooks->suspensionSubjects.size() == 3, "unexpected sibling source events");
    check(hooks->suspensionSubjects[0] == std::pair{SuspensionKind::event, hooks->subjects[0]}, "waiting sibling attribution");
    check(hooks->suspensionSubjects[1] == std::pair{SuspensionKind::yield, hooks->subjects[1]}, "borrowed TLS stole frame ownership");
    check(hooks->suspensionSubjects[2] == std::pair{SuspensionKind::yield, hooks->subjects[1]}, "resumed sibling attribution");
    check(sink->records[0].kind == NotificationKind::subject_started && sink->records[2].kind == NotificationKind::subject_started,
          "sibling subjects did not overlap");
    siblingContext = {};
}

std::thread       adoptedWorker;
gentest::Notifier retained;
std::uint64_t     adoptedSubject = 0;

void adoptedBody(void *) {
    const auto context = gentest::get_current_context();
    const auto subject = gentest::currentNotifier().subject();
    retained           = gentest::currentNotifier();
    std::latch acquired(1);
    adoptedWorker = std::thread([context, subject, &acquired] {
        auto lease     = gentest::set_current_context(context);
        adoptedSubject = gentest::currentNotifier().subject();
        acquired.count_down();
        // Owner stop happens after its callable returns. The run must join the
        // lease before publishing terminal lifecycle or the next occurrence.
        std::atomic<bool>  stopped{false};
        std::stop_callback callback(context.stop_token(), [&] {
            stopped.store(true);
            stopped.notify_one();
        });
        stopped.wait(false);
        check(adoptedSubject == subject, "adopted notifier attribution changed");
        (void)gentest::currentNotifier().progress("adopted cleanup");
    });
    acquired.wait();
}

void adoptedAndRepeatedSubjects() {
    auto sink  = std::make_shared<CaptureSink>();
    auto hooks = std::make_shared<Visitor>();
    auto test  = makeCase("notifications/adopted", &adoptedBody);
    check(run({&test, 1}, sink, hooks) == 0, "adopted context failed");
    adoptedWorker.join();
    check(adoptedSubject == hooks->subjects.front(), "adopted subject mismatch");
    check(sink->records[sink->size.load() - 2].kind == NotificationKind::progress, "terminal preceded adopted cleanup");
    check(sink->records[sink->size.load() - 1].kind == NotificationKind::subject_finished, "adopted terminal missing");
    const auto before = sink->size.load();
    check(retained.progress("stale") == NotifyResult::closed, "retained subject can renew after terminal");
    check(sink->size.load() == before, "stale notifier reached sink");

    sink                    = std::make_shared<CaptureSink>();
    hooks                   = std::make_shared<Visitor>();
    test                    = makeCase("notifications/repeated", +[](void *) {});
    const char *arguments[] = {"framework-events", "--no-color", "--repeat=512"};
    check(run({&test, 1}, sink, hooks, arguments) == 0, "repeat run failed");
    check(hooks->subjects.size() == 512, "repeat occurrence count");
    check(std::adjacent_find(hooks->subjects.begin(), hooks->subjects.end()) == hooks->subjects.end(), "repeat reused terminal subject");
}

NotifyResult  cleanupResult  = NotifyResult::disabled;
std::uint64_t cleanupSubject = 0;
struct FrameCleanup {
    gentest::Notifier notifier;
    ~FrameCleanup() {
        cleanupSubject = gentest::currentNotifier().subject();
        cleanupResult  = notifier.progress("frame cleanup");
    }
};

auto completedFrame(std::unique_ptr<FrameCleanup> parameter) -> gentest::awaitable<> {
    (void)parameter;
    co_return;
}

auto blockedFrame() -> gentest::awaitable<> {
    FrameCleanup               local{gentest::currentNotifier()};
    gentest::async::event<int> never;
    co_await never.wait("never published");
}

void physicalRetirementPrecedesTerminal() {
    for (const bool blocked : {false, true}) {
        cleanupResult  = NotifyResult::disabled;
        cleanupSubject = 0;
        auto sink      = std::make_shared<CaptureSink>();
        auto hooks     = std::make_shared<Visitor>();
        auto test      = makeCase("notifications/frame_cleanup");
        if (blocked) {
            test.async_fn = +[](void *) { return gentest::detail::make_async_task(blockedFrame()); };
        } else {
            test.async_fn = +[](void *) {
                auto parameter      = std::make_unique<FrameCleanup>();
                parameter->notifier = gentest::currentNotifier();
                return gentest::detail::make_async_task(completedFrame(std::move(parameter)));
            };
        }
        const auto result = run({&test, 1}, sink, hooks);
        check(blocked ? result != 0 : result == 0, "frame cleanup run result");
        check(cleanupResult == NotifyResult::accepted, "terminal notification preceded coroutine destruction");
        check(cleanupSubject == hooks->subjects.front(), "coroutine destruction lost owning context");
        check(sink->records[sink->size.load() - 2].kind == NotificationKind::progress &&
                  sink->records[sink->size.load() - 1].kind == NotificationKind::subject_finished,
              "frame cleanup ordering");
    }
}

bool                         insideMeasurement = false;
std::array<std::uint64_t, 3> measuredSubjects{};
void                         measuredBody(void *) {
    const auto phase        = gentest::detail::bench_phase();
    const auto index        = phase == gentest::detail::BenchPhase::Setup ? 0U : phase == gentest::detail::BenchPhase::Call ? 1U : 2U;
    measuredSubjects[index] = gentest::currentNotifier().subject();
    insideMeasurement       = phase == gentest::detail::BenchPhase::Call;
    if (insideMeasurement) {
        gentest::doNotOptimizeAway(index);
    }
    insideMeasurement = false;
}

void measurementBoundaries() {
    auto sink                = std::make_shared<CaptureSink>();
    auto hooks               = std::make_shared<Visitor>();
    hooks->insideMeasurement = &insideMeasurement;
    auto test                = makeCase("notifications/measured", &measuredBody);
    test.is_benchmark        = true;
    test.benchmark.execution = gentest::BenchmarkExecution::Once;
    const char *arguments[]  = {"framework-events", "--no-color", "--kind=bench"};
    check(run({&test, 1}, sink, hooks, arguments) == 0, "measured run failed");
    check(hooks->phaseStarts == 3 && hooks->phaseFinishes == 3, "measurement phase boundary count");
    check(!hooks->callbackInsideMeasurement, "notification entered timed loop");
    check(measuredSubjects[0] != 0 && measuredSubjects[0] == measuredSubjects[1] && measuredSubjects[1] == measuredSubjects[2],
          "measurement phases changed subjects");
}

unsigned      fixtureTeardowns = 0;
std::uint64_t blockingSubject  = 0;
void          fixtureSetup(void *, std::string &error) {
    const auto before = gentest::currentNotifier().subject();
    (void)gentest::detail::run_async_task_blocking(child(), "nested fixture", error);
    blockingSubject = gentest::currentNotifier().subject();
    check(before == blockingSubject && before != 0, "blocking fixture changed subject");
}
void fixtureTeardown(void *, std::string &) {
    ++fixtureTeardowns;
    (void)gentest::currentNotifier().progress("fixture cleanup");
}

void fixturesAndInfrastructureFailure() {
    gentest::detail::register_shared_fixture(
        gentest::detail::SharedFixtureScope::Global, "", "notification fixture",
        +[](std::string_view, std::string &) -> std::shared_ptr<void> { return std::make_shared<int>(1); }, &fixtureSetup,
        &fixtureTeardown);
    auto sink  = std::make_shared<CaptureSink>();
    auto hooks = std::make_shared<Visitor>();
    auto test  = makeCase("notifications/fixture", +[](void *) {});
    check(run({&test, 1}, sink, hooks) == 0, "fixture lifecycle failed");
    check(fixtureTeardowns == 1 && hooks->fixtureStarts == 2 && hooks->fixtureFinishes == 2, "shared fixture lifecycle incomplete");
    check(hooks->suspensions[static_cast<std::size_t>(SuspensionKind::yield)] == 1, "blocking scheduler source hook missing");
    check(sink->records[0].kind == NotificationKind::subject_started &&
              sink->records[sink->size.load() - 1].kind == NotificationKind::subject_finished,
          "run observation did not cover shared fixtures");

    // Delivery failure is infrastructure failure even if the case expects a
    // test failure. All fixture teardown remains required.
    sink               = std::make_shared<CaptureSink>();
    sink->failProgress = true;
    test               = makeCase(
        "notifications/failure", +[](void *) {
            gentest::xfail("expected test failure");
            (void)gentest::currentNotifier().progress("ignored failure");
            gentest::expect(false, "expected");
        });
    check(run({&test, 1}, sink) != 0, "notification failure hidden by xfail");
    check(fixtureTeardowns == 2, "notification failure skipped teardown");

    sink = std::make_shared<CaptureSink>();
    test = makeCase("notifications/invalid", +[](void *) { (void)gentest::currentNotifier().allowSilenceFor(0ns); });
    check(run({&test, 1}, sink) != 0, "pre-admission notification failure was ignored");
    check(fixtureTeardowns == 3, "validation failure skipped teardown");

    sink = std::make_shared<CaptureSink>();
    test = makeCase(
        "notifications/wrong_subject", +[](void *) {
            const auto notifier = gentest::currentNotifier();
            (void)notifier.forSubject(notifier.subject() + 1).progress("wrong case");
        });
    check(run({&test, 1}, sink) != 0, "active subject mismatch was ignored");
    check(fixtureTeardowns == 4, "subject mismatch skipped teardown");
}
} // namespace

int main() {
    try {
        suspensionBoundariesAndFiltering();
        disabledAndVisitorOnlyObservation();
        defaultVisitorAndLogs();
        concurrentSubjectsAndFrameOwnership();
        adoptedAndRepeatedSubjects();
        physicalRetirementPrecedesTerminal();
        measurementBoundaries();
        fixturesAndInfrastructureFailure();
        check(!gentest::currentNotifier().active(), "notifier escaped run scope");
    } catch (const std::exception &error) {
        // A diagnostic write failure must retain the failing process result.
        if (std::fprintf(stderr, "framework notification regression: %s\n", error.what()) < 0) {
            return 1;
        }
        return 1;
    }
    return 0;
}
