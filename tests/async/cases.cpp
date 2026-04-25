#include "gentest/attributes.h"
#include "gentest/runner.h"

#include <string>
#include <vector>

using namespace gentest::asserts;

namespace async {

gentest::async::manual_event server_ready;
gentest::async::manual_event client_done;
std::vector<std::string>     order;

struct LocalAsyncFixture : gentest::AsyncFixtureSetup, gentest::AsyncFixtureTearDown {
    int value = 0;

    gentest::async_test<void> setUp() override {
        co_await gentest::async::yield();
        value = 42;
    }

    gentest::async_test<void> tearDown() override {
        co_await gentest::async::yield();
        value = 0;
    }
};

struct LocalSyncFixture : gentest::FixtureSetup {
    int value = 0;

    void setUp() override { value = 7; }
};

[[using gentest: test("batch/server")]]
gentest::async_test<void> server() {
    order.clear();
    co_await gentest::async::yield();
    order.emplace_back("server");
    server_ready.set();
    co_await client_done.wait("client did not finish");
    ASSERT_EQ(order.size(), std::size_t{2});
    EXPECT_EQ(order[0], "server");
    EXPECT_EQ(order[1], "client");
}

[[using gentest: test("batch/client")]]
gentest::async_test<void> client() {
    co_await server_ready.wait("server was not selected");
    order.emplace_back("client");
    client_done.set();
    ASSERT_EQ(order.size(), std::size_t{2});
    EXPECT_EQ(order[0], "server");
    EXPECT_EQ(order[1], "client");
}

[[using gentest: test("value/discard")]]
gentest::async_test<int> value_discard() {
    co_await gentest::async::yield();
    EXPECT_TRUE(true);
    co_return 7;
}

[[using gentest: test("fixture/local_async")]]
gentest::async_test<void> local_async_fixture(LocalAsyncFixture &fixture) {
    EXPECT_EQ(fixture.value, 42);
    co_return;
}

[[using gentest: test("fixture/mixed")]]
gentest::async_test<void> mixed_fixtures(LocalSyncFixture &sync_fixture, LocalAsyncFixture &async_fixture) {
    EXPECT_EQ(sync_fixture.value, 7);
    EXPECT_EQ(async_fixture.value, 42);
    co_return;
}

[[using gentest: test("blocked/never")]]
gentest::async_test<void> blocked_never() {
    gentest::async::manual_event never;
    co_await never.wait("never ready");
}

} // namespace async
