#pragma once

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

inline gentest::async::manual_event long_driver_done;
inline gentest::async::manual_event sync_releases_async;
inline gentest::async::manual_event ping_turn;
inline gentest::async::manual_event pong_turn;
inline std::vector<std::string>     mix_events;

constexpr auto kVisiblePause   = 250ms;
constexpr auto kPingPongPause  = 600ms;
constexpr auto kPingPongRounds = 8;

inline auto count_log(std::string_view test_name, int count) -> std::string {
    return std::string(test_name) + " count " + std::to_string(count);
}

[[using gentest: test("panel/00_async_waits_for_sync")]]
gentest::async_test<void> async_waits_for_sync();

[[using gentest: test("panel/01_sync_releases_async")]]
void sync_releases_async_case();

[[using gentest: test("panel/02_short_pass")]]
gentest::async_test<void> short_pass();

[[using gentest: test("panel/03_medium_pass")]]
gentest::async_test<void> medium_pass();

[[using gentest: test("panel/04_long_driver")]]
gentest::async_test<void> long_driver();

[[using gentest: test("panel/05_waiting_on_driver")]]
gentest::async_test<void> waiting_on_driver();

[[using gentest: test("pingpong/00_ping")]]
gentest::async_test<void> ping_counter();

[[using gentest: test("pingpong/01_pong")]]
gentest::async_test<void> pong_counter();

} // namespace async_live_slow
