#pragma once

#include "gentest/async.h"
#include "gentest/test.h"

namespace async_examples::cases {

using namespace gentest;
using namespace gentest::async;

inline auto answer() -> awaitable<int> {
    co_await yield();
    co_return 42;
}

[[gentest::test("yield_and_return")]]
inline auto yieldAndReturn() -> awaitable<> {
    const int value = co_await answer();
    expect_eq(value, 42);
}

} // namespace async_examples::cases
