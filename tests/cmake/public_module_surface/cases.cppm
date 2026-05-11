module;

#include <coroutine>
#include <stop_token>
#include <type_traits>

export module public_module_surface.cases;

import gentest;

static_assert(std::is_same_v<decltype(gentest::get_current_context().stop_token()), std::stop_token>);
static_assert(std::is_same_v<decltype(gentest::get_current_context().stop_requested()), bool>);

export namespace public_module_surface {

[[using gentest: test("expect_true")]]
void expect_true_case() {
    gentest::expect_true(true);
}

[[using gentest: test("async_yield")]]
gentest::async_test<void> async_yield_case() {
    co_await gentest::async::yield();
    gentest::expect_true(true);
}

[[using gentest: test("async_event")]]
gentest::async_test<void> async_event_case() {
    gentest::async::event<int> event;
    event.set("module.event", 42);
    int &payload = co_await event.wait("module.event");
    gentest::expect_eq(payload, 42);
}

[[using gentest: test("async_promise")]]
gentest::async_test<void> async_promise_case() {
    gentest::async::promise<int> promise;
    auto                         future = promise.get_future();
    promise.set_value(42);
    int payload = co_await future;
    gentest::expect_eq(payload, 42);
}

} // namespace public_module_surface
