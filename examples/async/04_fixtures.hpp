#pragma once

#include "gentest/async.h"
#include "gentest/fixture.h"
#include "gentest/test.h"

namespace async_examples::fixtures {

using namespace gentest;
using namespace gentest::async;

// A local fixture is set up and torn down for each case.
struct Connection : AsyncFixtureSetup, AsyncFixtureTearDown {
    bool connected = false;

    auto setUp() -> awaitable<> override {
        co_await yield();
        connected = true;
    }

    auto tearDown() -> awaitable<> override {
        co_await yield();
        connected = false;
    }
};

// Shared infrastructure belongs in the namespace of all its consumers.
struct [[gentest::fixture(suite)]] Server : AsyncFixtureSetup {
    int port = 0;

    auto setUp() -> awaitable<> override {
        co_await yield();
        port = 8080;
    }
};

[[gentest::test("local_connection")]]
inline auto localConnection(Connection &connection, Server &server) -> awaitable<> {
    expect_true(connection.connected);
    expect_eq(server.port, 8080);
    co_return;
}

[[gentest::test("shared_server")]]
inline auto sharedServer(Server &server) -> awaitable<> {
    co_await yield();
    expect_eq(server.port, 8080);
}

} // namespace async_examples::fixtures
