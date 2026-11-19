#include "gentest/assertions.h"
#include "gentest/context.h"
#include "gentest/runner.h"

#include <chrono>
#include <coroutine>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;

void death(void *) {
    if (std::fputs("fatal diagnostic\n", stderr) == EOF)
        return;
    std::_Exit(77);
}

void failedExpectation(void *) {
    gentest::expect(false, "unexpected value before exit");
    std::_Exit(23);
}

// NOLINTNEXTLINE(bugprone-exception-escape): exercise termination when fail() escapes a noexcept body.
void noexceptFail(void *) noexcept { gentest::fail("explicit failure in noexcept helper"); }

void unwindFail(void *) {
    struct ExitOnUnwind {
        ~ExitOnUnwind() {
            if (std::uncaught_exceptions() > 0)
                std::_Exit(23);
        }
    } guard;
    gentest::fail("explicit failure before unwinding");
}

void diagnostic(void *) {
    if (std::fputs("[ FAIL ] parser rejected input\n", stderr) != EOF)
        std::_Exit(23);
}

void stall(void *) {
    (void)gentest::currentNotifier().progress("waiting for input");
    for (;;)
        std::this_thread::sleep_for(1s);
}

void silent(void *) {
    const auto result = gentest::currentNotifier().allowSilenceFor(10s, "bounded wait");
    if (result != gentest::NotifyResult::accepted)
        return;
    std::this_thread::sleep_for(1800ms);
    std::_Exit(42);
}

void malformed(void *) {
    (void)gentest::currentNotifier().allowSilenceFor(0ms);
    std::_Exit(42);
}

void invalid(void *) {
    // This exceeds the watchdog's maximum silence allowance. The parent must
    // retain the protocol failure even when the child exits immediately.
    (void)gentest::currentNotifier().allowSilenceFor(24h, "invalid wait");
    std::_Exit(42);
}

enum class AsyncExit { Body, NoexceptFail, FailedExpectation, FrameTeardown, CanceledFrameTeardown };

struct ExitOnFrameRelease {
    ~ExitOnFrameRelease() {
        if (std::fputs("async frame teardown\n", stderr) != EOF)
            std::_Exit(42);
    }
};

// A coroutine parameter lives until the runner destroys its frame, after
// final_suspend on return or during cancellation of an unresumable task.
gentest::awaitable<void> asyncBody(std::unique_ptr<ExitOnFrameRelease>, AsyncExit phase) {
    co_await gentest::async::yield();
    if (phase == AsyncExit::NoexceptFail)
        noexceptFail(nullptr);
    if (phase == AsyncExit::FailedExpectation) {
        gentest::expect(false, "unexpected value after resume");
        std::_Exit(42);
    }
    if (phase == AsyncExit::Body && std::fputs("async body resumed\n", stderr) != EOF)
        std::_Exit(42);
    if (phase == AsyncExit::CanceledFrameTeardown)
        co_await std::suspend_always{};
}

template <AsyncExit phase> auto asyncCase(void *) -> gentest::detail::AsyncTaskPtr {
    return gentest::detail::make_async_task(asyncBody(std::make_unique<ExitOnFrameRelease>(), phase));
}
} // namespace

int main(int argc, char **argv) {
    const gentest::Case cases[]{
        {.name = "probe/death", .fn = death, .is_death = true},
        {.name = "probe/ordinary", .fn = death},
        {.name = "probe/caf\xc3\xa9", .fn = death, .is_death = true},
        {.name = "probe/failed_expectation", .fn = failedExpectation, .is_death = true},
        {.name = "probe/noexcept_fail", .fn = noexceptFail, .is_death = true},
        {.name = "probe/unwind_fail", .fn = unwindFail, .is_death = true},
        {.name = "probe/async_noexcept_fail", .async_fn = asyncCase<AsyncExit::NoexceptFail>, .is_async = true, .is_death = true},
        {.name = "probe/diagnostic", .fn = diagnostic, .is_death = true},
        {.name = "probe/stall", .fn = stall, .is_death = true},
        {.name = "probe/silence", .fn = silent, .is_death = true},
        {.name = "probe/malformed", .fn = malformed, .is_death = true},
        {.name = "probe/invalid", .fn = invalid, .is_death = true},
        {.name = "probe/async_body", .async_fn = asyncCase<AsyncExit::Body>, .is_async = true, .is_death = true},
        {.name = "probe/async_expectation", .async_fn = asyncCase<AsyncExit::FailedExpectation>, .is_async = true, .is_death = true},
        {.name = "probe/async_teardown", .async_fn = asyncCase<AsyncExit::FrameTeardown>, .is_async = true, .is_death = true},
        {.name = "probe/async_canceled", .async_fn = asyncCase<AsyncExit::CanceledFrameTeardown>, .is_async = true, .is_death = true},
    };
    std::vector<const char *> args(argv, argv + argc);
    return gentest::run_cases(cases, args);
}
