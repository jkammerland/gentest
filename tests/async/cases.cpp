#include "gentest/attributes.h"
#include "gentest/runner.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace gentest::asserts;

namespace async {

gentest::async::manual_event server_ready;
gentest::async::manual_event client_done;
std::vector<std::string>     order;

gentest::async::manual_event live_demo_resume;
std::vector<std::string>     live_demo_order;

constexpr int kLiveDemoRounds = 10;

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

[[using gentest: test("live_demo/00_suspended")]]
gentest::async_test<void> live_demo_suspended() {
    live_demo_order.clear();
    live_demo_resume.reset();
    live_demo_order.emplace_back("suspended:start");

    co_await live_demo_resume.wait("waiting for live demo driver");

    live_demo_order.emplace_back("suspended:resumed");
    ASSERT_EQ(live_demo_order.size(), std::size_t{3});
    EXPECT_EQ(live_demo_order[0], "suspended:start");
    EXPECT_EQ(live_demo_order[1], "driver:complete");
    EXPECT_EQ(live_demo_order[2], "suspended:resumed");
}

[[using gentest: test("live_demo/01_driver")]]
gentest::async_test<void> live_demo_driver() {
    for (int i = 0; i < kLiveDemoRounds; ++i) {
        co_await gentest::async::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
    }

    live_demo_order.emplace_back("driver:complete");
    live_demo_resume.set();
}

[[using gentest: test("fail_fast/00_async_fail")]]
gentest::async_test<void> fail_fast_async_fail() {
    co_await gentest::async::yield();
    EXPECT_TRUE(false);
}

[[using gentest: test("fail_fast/01_sync_should_not_run")]]
void fail_fast_sync_should_not_run() {
    gentest::fail("fail-fast allowed a later sync case to run");
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
