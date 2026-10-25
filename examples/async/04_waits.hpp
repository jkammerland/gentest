#pragma once

#include "gentest/async.h"
#include "gentest/test.h"

#include <chrono>
#include <memory>

namespace async_examples::waits {

using namespace gentest;
using namespace gentest::async;

[[gentest::test("one_shot_value")]]
inline auto oneShotValue() -> async_test<void> {
    promise<std::unique_ptr<int>> reply;
    auto                          response = reply.get_future();

    reply.set_value(std::make_unique<int>(42));
    auto value = co_await std::move(response);
    expect_eq(*value, 42);
}

[[gentest::test("bounded_event")]]
inline auto boundedEvent() -> async_test<void> {
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
inline auto timeoutRetiresOnlyThisWait() -> async_test<void> {
    using namespace std::chrono_literals;
    event<int> replies;

    auto missing = co_await wait_for(replies.wait("missing"), 1ms);
    expect_true(missing.timed_out());

    // The channel still works after a subscription times out.
    replies.set("next", 7);
    const int value = co_await replies.wait("next");
    expect_eq(value, 7);
}

} // namespace async_examples::waits
