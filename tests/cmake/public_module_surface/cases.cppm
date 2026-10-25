module;

#include <array>
#include <chrono>
#include <cmath>
#include <coroutine>
#include <fmt/format.h>
#include <memory>
#include <optional>
#include <ostream>
#include <stop_token>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <variant>

namespace public_module_surface_support {

struct FmtOnlyValue {
    int value;
};

struct StreamOnlyValue {
    int value;
};

std::ostream &operator<<(std::ostream &stream, const StreamOnlyValue &value) {
    return stream << "module-stream-value(" << value.value << ')';
}

} // namespace public_module_surface_support

template <> struct fmt::formatter<public_module_surface_support::FmtOnlyValue> : fmt::formatter<std::string_view> {
    auto format(const public_module_surface_support::FmtOnlyValue &value, fmt::format_context &ctx) const {
        return fmt::format_to(ctx.out(), "module-fmt-value({})", value.value);
    }
};

export module public_module_surface.cases;

import gentest;

static_assert(std::is_same_v<decltype(gentest::get_current_context().stop_token()), std::stop_token>);
static_assert(std::is_same_v<decltype(gentest::get_current_context().stop_requested()), bool>);

export namespace public_module_surface {

struct BenchmarkFixture : gentest::FixtureTearDown {
    int  calls = 0;
    void tearDown() override {
        gentest::expect_eq(calls, 1);
        gentest::recordTimingSamples("device", std::array{std::chrono::nanoseconds{0}, std::chrono::nanoseconds{10}});
        auto snapshot = gentest::currentMeasurement();
        gentest::require(snapshot.has_value());
        gentest::expect_eq(snapshot->primary.items_per_call, 1000U);
        gentest::require(snapshot->primary.statistics.duration.has_value());
        gentest::expect_eq(snapshot->primary.statistics.duration->p99_ns, 2e9);
        gentest::require(snapshot->timings.size() == 1);
        gentest::require(snapshot->timings[0].statistics.duration.has_value());
        gentest::expect_eq(snapshot->timings[0].statistics.duration->mean_ns, 5.0);
        gentest::expect_true(std::abs(snapshot->timings[0].statistics.duration->p99_ns - 9.9) < 1e-12);
    }
};

[[using gentest: bench("measurement"), bench_once, bench_time("supplied")]]
void benchmark_measurement(BenchmarkFixture &sample) {
    ++sample.calls;
    gentest::setBenchmarkDuration(std::chrono::seconds{2}, 1000);
}

[[using gentest: test("expect_true")]]
void expect_true_case() {
    gentest::expect_true(true);
}

[[using gentest: test("format_value")]]
void format_value_case() {
    gentest::expect_eq(gentest::format_value(public_module_surface_support::FmtOnlyValue{42}), "module-fmt-value(42)");
    gentest::expect_eq(gentest::format_value(public_module_surface_support::StreamOnlyValue{7}), "module-stream-value(7)");
}

[[using gentest: test("async_yield")]]
gentest::task<> async_yield_case() {
    co_await gentest::async::yield();
    gentest::expect_true(true);
}

[[using gentest: test("async_event")]]
gentest::task<void> async_event_case() {
    gentest::async::event<int> event;
    event.set("module.event", 42);
    int &payload = co_await event.wait("module.event");
    gentest::expect_eq(payload, 42);
}

[[using gentest: test("async_promise")]]
gentest::task<void> async_promise_case() {
    gentest::async::promise<int> promise;
    auto                         future = promise.get_future();
    promise.set_value(42);
    int payload = co_await future;
    gentest::expect_eq(payload, 42);

    gentest::async::promise<int> direct;
    direct.set_value(43);
    auto timed = co_await gentest::async::wait_for(direct.get_future(), std::chrono::seconds{1});
    gentest::require(timed.ready());
    gentest::expect_eq(timed.value(), 43);

    std::stop_source stop;
    stop.request_stop();
    bool canceled = false;
    try {
        co_await gentest::async::sleep_for(std::chrono::hours{1}, stop.get_token());
    } catch (const gentest::async::operation_cancelled &) { canceled = true; }
    gentest::expect_true(canceled);
}

struct CallbackWait {
    std::unique_ptr<int> value;
    bool                 registration_finished        = false;
    bool                 delivered_after_registration = false;

    bool                          await_ready() const noexcept { return false; }
    template <class Promise> void await_suspend(std::coroutine_handle<Promise> parent) {
        auto next = parent.promise().make_continuation();
        auto callback =
            gentest::async::bind_continuation<std::unique_ptr<int>>(std::move(next), [this](std::unique_ptr<int> result) noexcept {
                value                        = std::move(result);
                delivered_after_registration = registration_finished;
            });
        callback(std::make_unique<int>(57));
        registration_finished = true;
    }
    auto await_resume() -> std::unique_ptr<int> {
        gentest::expect_true(delivered_after_registration);
        return std::move(value);
    }
};

[[using gentest: test("async_callback")]]
gentest::task<void> async_callback_case() {
    auto result = co_await gentest::async::wait_for(CallbackWait{}, std::chrono::seconds{1});
    gentest::require(result.ready());
    gentest::expect_eq(*std::move(result).value(), 57);

    auto direct = co_await CallbackWait{};
    gentest::require(direct != nullptr);
    gentest::expect_eq(*direct, 57);

    auto all = co_await gentest::async::when_all(CallbackWait{}, CallbackWait{});
    gentest::expect_eq(*std::get<0>(all) + *std::get<1>(all), 114);
    auto first = co_await gentest::async::when_any(CallbackWait{}, CallbackWait{});
    gentest::expect_eq(first.index(), std::size_t{0});
    gentest::expect_eq(*std::get<0>(first), 57);
}

} // namespace public_module_surface
