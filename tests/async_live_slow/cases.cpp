#include "gentest/attributes.h"
#include "gentest/runner.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace gentest::asserts;
using namespace std::chrono_literals;

namespace async_live_slow {

gentest::async::manual_event long_driver_done;
gentest::async::manual_event sync_releases_async;
std::vector<std::string>     mix_events;

constexpr auto kVisiblePause = 250ms;

[[using gentest: test("panel/00_async_waits_for_sync")]]
gentest::async_test<void> async_waits_for_sync() {
    mix_events.clear();
    long_driver_done.reset();
    sync_releases_async.reset();
    mix_events.emplace_back("async:start");
    co_await sync_releases_async.wait("waiting for sync case");
    mix_events.emplace_back("async:resumed");
    ASSERT_EQ(mix_events.size(), std::size_t{3});
    EXPECT_EQ(mix_events[0], "async:start");
    EXPECT_EQ(mix_events[1], "sync:ran");
    EXPECT_EQ(mix_events[2], "async:resumed");
}

[[using gentest: test("panel/01_sync_releases_async")]]
void sync_releases_async_case() {
    ASSERT_EQ(mix_events.size(), std::size_t{1});
    EXPECT_EQ(mix_events[0], "async:start");
    mix_events.emplace_back("sync:ran");
    sync_releases_async.set();
}

[[using gentest: test("panel/02_short_pass")]]
gentest::async_test<void> short_pass() {
    std::this_thread::sleep_for(kVisiblePause);
    co_await gentest::async::yield();
    EXPECT_TRUE(true);
}

[[using gentest: test("panel/03_medium_pass")]]
gentest::async_test<void> medium_pass() {
    for (int i = 0; i < 4; ++i) {
        std::this_thread::sleep_for(kVisiblePause);
        co_await gentest::async::yield();
    }
    EXPECT_TRUE(true);
}

[[using gentest: test("panel/04_long_driver")]]
gentest::async_test<void> long_driver() {
    for (int i = 0; i < 14; ++i) {
        std::this_thread::sleep_for(kVisiblePause);
        co_await gentest::async::yield();
    }
    long_driver_done.set();
    EXPECT_TRUE(true);
}

[[using gentest: test("panel/05_waiting_on_driver")]]
gentest::async_test<void> waiting_on_driver() {
    co_await long_driver_done.wait("waiting for long driver");
    EXPECT_TRUE(true);
}

} // namespace async_live_slow
