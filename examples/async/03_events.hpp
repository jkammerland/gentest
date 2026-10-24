#pragma once

#include "gentest/async.h"
#include "gentest/fixture.h"
#include "gentest/test.h"

#include <string>

namespace async_examples::events {

struct Connected {
    std::string peer;
    int         port;
};

// Explicit channels carry typed values under application-defined names.
// A real service can publish from its callback instead of setUp().
struct [[gentest::fixture(suite)]] Notifications : gentest::FixtureSetup {
    gentest::async::event<Connected> connected;

    void setUp() override { connected.set("primary", Connected{.peer = "localhost", .port = 8080}); }
};

[[gentest::test("named_typed_value")]]
inline auto namedTypedValue(Notifications &notifications) -> gentest::async_test<void> {
    const auto &message = co_await notifications.connected.wait("primary");
    gentest::expect_eq(message.peer, std::string{"localhost"});
    gentest::expect_eq(message.port, 8080);
}

// Waiting observes the latched value; it does not consume another case's message.
[[gentest::test("another_observer")]]
inline auto anotherObserver(Notifications &notifications) -> gentest::async_test<void> {
    co_await gentest::async::yield();
    const auto &message = co_await notifications.connected.wait("primary");
    gentest::expect_eq(message.port, 8080);
}

} // namespace async_examples::events
