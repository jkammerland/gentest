#pragma once

#include "gentest/async.h"
#include "gentest/test.h"

namespace async_examples::cases {

inline auto answer() -> gentest::async_test<int> {
    co_await gentest::async::yield();
    co_return 42;
}

[[gentest::test("yield_and_return")]]
inline auto yieldAndReturn() -> gentest::async_test<void> {
    const int value = co_await answer();
    gentest::expect_eq(value, 42);
}

} // namespace async_examples::cases
