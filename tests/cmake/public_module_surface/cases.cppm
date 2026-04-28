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

} // namespace public_module_surface
