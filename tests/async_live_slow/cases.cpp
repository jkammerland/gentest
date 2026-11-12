#include "cases.hpp"

#include <cstdlib>

namespace async_live_slow {
namespace {

// Automated terminal checks still span redraw frames and every handoff; only
// the pacing for a person watching the demo changes in this explicit mode.
auto demoPause(std::chrono::milliseconds visible_pause) -> std::chrono::milliseconds {
#if defined(_WIN32)
    char       *value  = nullptr;
    std::size_t length = 0;
    const bool  fast   = _dupenv_s(&value, &length, "GENTEST_ASYNC_DEMO_FAST") == 0 && value && std::string_view(value) == "1";
    std::free(value);
#else
    const auto *value = std::getenv("GENTEST_ASYNC_DEMO_FAST");
    const bool  fast  = value && std::string_view(value) == "1";
#endif
    return fast ? 50ms : visible_pause;
}

} // namespace

gentest::awaitable<void> async_waits_for_sync() {
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

} // namespace async_live_slow

namespace async_live_slow {

void sync_releases_async_case() {
    ASSERT_EQ(mix_events.size(), std::size_t{1});
    EXPECT_EQ(mix_events[0], "async:start");
    mix_events.emplace_back("sync:ran");
    sync_releases_async.set("sync case released async case");
}

} // namespace async_live_slow

namespace async_live_slow {

gentest::awaitable<void> short_pass() {
    gentest::log("short async case started");
    std::this_thread::sleep_for(demoPause(kVisiblePause));
    co_await gentest::async::yield();
    gentest::log("short async case resumed");
    EXPECT_TRUE(true);
}

} // namespace async_live_slow

namespace async_live_slow {

gentest::awaitable<void> medium_pass() {
    for (int i = 0; i < 4; ++i) {
        gentest::log("medium async tick " + std::to_string(i));
        std::this_thread::sleep_for(demoPause(kVisiblePause));
        co_await gentest::async::yield();
    }
    EXPECT_TRUE(true);
}

} // namespace async_live_slow

namespace async_live_slow {

gentest::awaitable<void> long_driver() {
    for (int i = 0; i < 14; ++i) {
        gentest::log("long async driver tick " + std::to_string(i));
        std::this_thread::sleep_for(demoPause(kVisiblePause));
        co_await gentest::async::yield();
    }
    long_driver_done.set("long driver completed");
    EXPECT_TRUE(true);
}

} // namespace async_live_slow

namespace async_live_slow {

gentest::awaitable<void> waiting_on_driver() {
    co_await long_driver_done.wait("long driver completed");
    EXPECT_TRUE(true);
}

} // namespace async_live_slow

namespace async_live_slow {

gentest::awaitable<void> ping_counter() {
    ping_turn.reset_all();
    pong_turn.reset_all();
    ping_turn.set("pingpong.turn");
    for (int i = 0; i < kPingPongRounds; ++i) {
        co_await ping_turn.wait("pingpong.turn");
        ping_turn.reset("pingpong.turn");
        gentest::log(count_log("async_live_slow/pingpong/00_ping", i));
        std::this_thread::sleep_for(demoPause(kPingPongPause));
        pong_turn.set("pingpong.turn");
        co_await gentest::async::yield();
    }
    EXPECT_TRUE(true);
}

} // namespace async_live_slow

namespace async_live_slow {

gentest::awaitable<void> pong_counter() {
    for (int i = 0; i < kPingPongRounds; ++i) {
        co_await pong_turn.wait("pingpong.turn");
        pong_turn.reset("pingpong.turn");
        gentest::log(count_log("async_live_slow/pingpong/01_pong", i));
        std::this_thread::sleep_for(demoPause(kPingPongPause));
        ping_turn.set("pingpong.turn");
        co_await gentest::async::yield();
    }
    EXPECT_TRUE(true);
}

} // namespace async_live_slow
