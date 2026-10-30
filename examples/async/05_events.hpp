#pragma once

#include "gentest/async.h"
#include "gentest/fixture.h"
#include "gentest/test.h"

#include <memory>
#include <string>

namespace async_examples::events {

using namespace gentest;
using namespace gentest::async;

struct Connected {
    std::string peer;
    int         port;
};

// Explicit channels carry typed values under application-defined names.
// A real service can publish from its callback instead of setUp().
struct [[gentest::fixture(suite)]] Notifications : FixtureSetup {
    event<Connected> connected;

    void setUp() override { connected.set("primary", Connected{.peer = "localhost", .port = 8080}); }
};

[[gentest::test("named_typed_value")]]
inline auto namedTypedValue(Notifications &notifications) -> awaitable<> {
    const auto &message = co_await notifications.connected.wait("primary");
    expect_eq(message.peer, std::string{"localhost"});
    expect_eq(message.port, 8080);
}

// Waiting observes the latched value; it does not consume another case's message.
[[gentest::test("another_observer")]]
inline auto anotherObserver(Notifications &notifications) -> awaitable<> {
    co_await yield();
    const auto &message = co_await notifications.connected.wait("primary");
    expect_eq(message.port, 8080);
}

[[gentest::test("one_shot_value")]]
inline auto oneShotValue() -> awaitable<> {
    promise<std::unique_ptr<int>> reply;
    auto                          response = reply.get_future();

    reply.set_value(std::make_unique<int>(42));
    auto value = co_await std::move(response);
    expect_eq(*value, 42);
}

} // namespace async_examples::events
