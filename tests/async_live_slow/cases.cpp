#include "gentest/attributes.h"
#include "gentest/runner.h"

#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace gentest::asserts;
using namespace std::chrono_literals;

namespace async_live_slow {

gentest::async::manual_event long_driver_done;
gentest::async::manual_event sync_releases_async;
gentest::async::manual_event ping_turn;
gentest::async::manual_event pong_turn;
std::vector<std::string>     mix_events;

constexpr auto kVisiblePause   = 250ms;
constexpr auto kPingPongPause  = 180ms;
constexpr auto kPingPongRounds = 8;

auto count_log(std::string_view test_name, int count) -> std::string { return std::string(test_name) + " count " + std::to_string(count); }

[[using gentest: test("panel/00_async_waits_for_sync")]]
gentest::async_test<void> async_waits_for_sync() {
    mix_events.clear();
    long_driver_done.reset_all();
    sync_releases_async.reset_all();
    mix_events.emplace_back("async:start");
    co_await sync_releases_async.wait("sync case released async case");
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
    sync_releases_async.set("sync case released async case");
}

[[using gentest: test("panel/02_short_pass")]]
gentest::async_test<void> short_pass() {
    gentest::log("short async case started");
    std::this_thread::sleep_for(kVisiblePause);
    co_await gentest::async::yield();
    gentest::log("short async case resumed");
    EXPECT_TRUE(true);
}

[[using gentest: test("panel/03_medium_pass")]]
gentest::async_test<void> medium_pass() {
    for (int i = 0; i < 4; ++i) {
        gentest::log("medium async tick " + std::to_string(i));
        std::this_thread::sleep_for(kVisiblePause);
        co_await gentest::async::yield();
    }
    EXPECT_TRUE(true);
}

[[using gentest: test("panel/04_long_driver")]]
gentest::async_test<void> long_driver() {
    for (int i = 0; i < 14; ++i) {
        gentest::log("long async driver tick " + std::to_string(i));
        std::this_thread::sleep_for(kVisiblePause);
        co_await gentest::async::yield();
    }
    long_driver_done.set("long driver completed");
    EXPECT_TRUE(true);
}

[[using gentest: test("panel/05_waiting_on_driver")]]
gentest::async_test<void> waiting_on_driver() {
    co_await long_driver_done.wait("long driver completed");
    EXPECT_TRUE(true);
}

[[using gentest: test("pingpong/00_ping")]]
gentest::async_test<void> ping_counter() {
    ping_turn.reset_all();
    pong_turn.reset_all();
    ping_turn.set("pingpong.turn");
    for (int i = 0; i < kPingPongRounds; ++i) {
        co_await ping_turn.wait("pingpong.turn");
        ping_turn.reset("pingpong.turn");
        gentest::log(count_log("async_live_slow/pingpong/00_ping", i));
        std::this_thread::sleep_for(kPingPongPause);
        pong_turn.set("pingpong.turn");
        co_await gentest::async::yield();
    }
    EXPECT_TRUE(true);
}

[[using gentest: test("pingpong/01_pong")]]
gentest::async_test<void> pong_counter() {
    for (int i = 0; i < kPingPongRounds; ++i) {
        co_await pong_turn.wait("pingpong.turn");
        pong_turn.reset("pingpong.turn");
        gentest::log(count_log("async_live_slow/pingpong/01_pong", i));
        std::this_thread::sleep_for(kPingPongPause);
        ping_turn.set("pingpong.turn");
        co_await gentest::async::yield();
    }
    EXPECT_TRUE(true);
}

} // namespace async_live_slow
