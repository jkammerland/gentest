#pragma once

#include "gentest/async.h"
#include "gentest/test.h"

#include <chrono>
#include <memory>

namespace async_examples::waits {

[[gentest::test("one_shot_value")]]
inline auto oneShotValue() -> gentest::async_test<void> {
    gentest::async::promise<std::unique_ptr<int>> reply;
    auto                                          response = reply.get_future();

    reply.set_value(std::make_unique<int>(42));
    auto value = co_await std::move(response);
    gentest::expect_eq(*value, 42);
}

[[gentest::test("bounded_event")]]
inline auto boundedEvent() -> gentest::async_test<void> {
    using namespace std::chrono_literals;
    gentest::async::event<int> replies;
    replies.set("request-1", 42);

    auto result = co_await gentest::async::wait_for(replies.wait("request-1"), 1s);
    if (result.timed_out()) {
        gentest::fail("request-1 did not receive a reply");
        co_return;
    }
    gentest::expect_eq(result.value(), 42);
}

[[gentest::test("timeout_retires_only_this_wait")]]
inline auto timeoutRetiresOnlyThisWait() -> gentest::async_test<void> {
    using namespace std::chrono_literals;
    gentest::async::event<int> replies;

    auto missing = co_await gentest::async::wait_for(replies.wait("missing"), 1ms);
    gentest::expect_true(missing.timed_out());

    // The channel still works after a subscription times out.
    replies.set("next", 7);
    const int value = co_await replies.wait("next");
    gentest::expect_eq(value, 7);
}

} // namespace async_examples::waits
