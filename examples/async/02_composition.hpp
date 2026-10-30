#pragma once

#include "gentest/async.h"
#include "gentest/test.h"

#include <chrono>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>

namespace async_examples::composition {

using namespace gentest;
using namespace gentest::async;

inline auto answer(int value) -> awaitable<int> {
    co_await yield();
    co_return value;
}

inline auto markComplete(bool &completed) -> awaitable<> {
    co_await yield();
    completed = true;
}

[[gentest::test("collect_all_results")]]
inline auto collectAllResults() -> awaitable<> {
    bool completed             = false;
    auto [first, second, done] = co_await when_all(answer(19), answer(23), markComplete(completed));

    expect_eq(first, 19);
    expect_eq(second, 23);
    expect_true(completed);
    static_assert(std::is_same_v<decltype(done), std::monostate>);
}

inline auto longOperation(bool &stopped) -> awaitable<std::string> {
    using namespace std::chrono_literals;

    try {
        co_await sleep_for(1h);
    } catch (const operation_cancelled &) {
        stopped = true;
        throw;
    }
    co_return "late reply";
}

[[gentest::test("first_result_joins_loser")]]
inline auto firstResultJoinsLoser() -> awaitable<> {
    bool stopped = false;
    auto result  = co_await when_any(longOperation(stopped), answer(42));

    // The losing operation has observed stop and finished before when_any returns.
    expect_eq(result.index(), std::size_t{1});
    expect_eq(std::get<1>(result), 42);
    expect_true(stopped);
}

} // namespace async_examples::composition
