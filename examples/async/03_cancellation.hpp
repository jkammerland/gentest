#pragma once

#include "gentest/async.h"
#include "gentest/test.h"

#include <chrono>
#include <stop_token>

namespace async_examples::waits {

using namespace gentest;
using namespace gentest::async;

[[gentest::test("bounded_event")]]
inline auto boundedEvent() -> task<> {
    using namespace std::chrono_literals;
    event<int> replies;
    replies.set("request-1", 42);

    auto result = co_await wait_for(replies.wait("request-1"), 1s);
    if (result.timed_out()) {
        fail("request-1 did not receive a reply");
        co_return;
    }
    expect_eq(result.value(), 42);
}

[[gentest::test("timeout_retires_only_this_wait")]]
inline auto timeoutRetiresOnlyThisWait() -> task<> {
    using namespace std::chrono_literals;
    event<int> replies;

    auto missing = co_await wait_for(replies.wait("missing"), 1ms);
    expect_true(missing.timed_out());

    // The channel still works after a subscription times out.
    replies.set("next", 7);
    const int value = co_await replies.wait("next");
    expect_eq(value, 7);
}

[[gentest::test("explicit_stop_token")]]
inline auto explicitStopToken() -> task<> {
    event<int>       pending;
    std::stop_source stop;
    stop.request_stop();

    bool cancelled = false;
    try {
        (void)co_await pending.wait("reply", stop.get_token());
    } catch (const operation_cancelled &) { cancelled = true; }
    expect_true(cancelled);

    // An explicit empty token permits a cleanup wait after cancellation.
    co_await yield(std::stop_token{});
}

} // namespace async_examples::waits
