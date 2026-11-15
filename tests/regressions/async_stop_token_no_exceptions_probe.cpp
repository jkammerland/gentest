// Compile-only probe driven by tests/cmake/scripts/CheckAsyncStopTokenNoExceptions.cmake.
// Without PROBE it exercises the waits that exception-disabled builds keep;
// each PROBE value selects one explicit stop-token overload.
#include "gentest/async.h"

#include <chrono>
#include <stop_token>

namespace {
[[maybe_unused]] auto probe(gentest::async::manual_event &replies, gentest::async::future<int> &value, gentest::async::future<void> &done)
    -> gentest::awaitable<void> {
    using namespace std::chrono_literals;
    [[maybe_unused]] const std::stop_token token;
#if !defined(PROBE)
    co_await gentest::async::yield();
    co_await gentest::async::sleep_for(1ms);
    co_await gentest::async::sleep_until(std::chrono::steady_clock::now());
    (void)co_await replies.wait("reply");
    (void)co_await value.wait();
    co_await done.wait("named reason");
    (void)co_await gentest::async::when_all(gentest::async::yield(), gentest::async::sleep_for(1ms));
#elif PROBE == 1
    co_await gentest::async::yield(token);
#elif PROBE == 2
    co_await gentest::async::sleep_for(1ms, token);
#elif PROBE == 3
    co_await gentest::async::sleep_until(std::chrono::steady_clock::now(), token);
#elif PROBE == 4
    (void)co_await replies.wait("reply", token);
#elif PROBE == 5
    (void)co_await value.wait(token);
#elif PROBE == 6
    co_await done.wait(token);
#endif
}
} // namespace
