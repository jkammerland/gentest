module;

#include <coroutine>

export module public_module_surface.cases;

import gentest;

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

} // namespace public_module_surface
