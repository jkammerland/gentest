#pragma once

#include "gentest/async.h"
#include "gentest/fixture.h"
#include "gentest/test.h"

namespace async_examples::fixtures {

// A local fixture is set up and torn down for each case.
struct Connection : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    bool connected = false;

    auto setUp() -> gentest::async_test<void> override {
        co_await gentest::async::yield();
        connected = true;
    }

    auto tearDown() -> gentest::async_test<void> override {
        co_await gentest::async::yield();
        connected = false;
    }
};

// Shared infrastructure belongs in the namespace of all its consumers.
struct [[gentest::fixture(suite)]] Server : gentest::AsyncFixtureSetup {
    int port = 0;

    auto setUp() -> gentest::async_test<void> override {
        co_await gentest::async::yield();
        port = 8080;
    }
};

[[gentest::test("local_connection")]]
inline auto localConnection(Connection &connection, Server &server) -> gentest::async_test<void> {
    gentest::expect_true(connection.connected);
    gentest::expect_eq(server.port, 8080);
    co_return;
}

[[gentest::test("shared_server")]]
inline auto sharedServer(Server &server) -> gentest::async_test<void> {
    co_await gentest::async::yield();
    gentest::expect_eq(server.port, 8080);
}

} // namespace async_examples::fixtures
