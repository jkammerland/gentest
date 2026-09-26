#pragma once

#include "gentest/bench_util.h"
#include "gentest/fixture.h"
#include "gentest/test.h"

#include <array>
#include <cstddef>
#include <span>

namespace measured {

// The same implementation is checked for correctness and measured below.
template <typename T, bool Reverse> T sum(std::span<const int> values) {
    T result{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto index = Reverse ? values.size() - 1 - i : i;
        result += static_cast<T>(values[index]);
    }
    return result;
}

struct Input : gentest::FixtureSetup {
    std::array<int, 64> values{};

    // Fixture preparation runs outside the timed loop for both bench and jitter.
    void setUp() override {
        for (std::size_t i = 0; i < values.size(); ++i) {
            values[i] = static_cast<int>(i + 1);
        }
    }
};

template <typename T, bool Reverse>
[[using gentest: test("sum_correct"), template(T, int, long), template(Reverse, false, true), req("SUM-001"), owner("examples"), fast]]
void sumCorrect(Input &input) {
    gentest::expect_eq(sum<T, Reverse>({}), T{0});
    gentest::expect_eq(sum<T, Reverse>(std::span<const int>(input.values).first(1)), T{1});
    gentest::expect_eq(sum<T, Reverse>(input.values), T{2080});
}

template <typename T, bool Reverse>
[[using gentest: bench("sum_throughput"), template(T, int, long), template(Reverse, false, true), items_per_call(64), owner("examples")]]
void sumThroughput(Input &input) {
    gentest::clobberMemory();
    auto result = sum<T, Reverse>(input.values);
    gentest::doNotOptimizeAway(result);
}

template <typename T, bool Reverse>
[[using gentest: jitter("sum_latency"), template(T, int, long), template(Reverse, false, true), items_per_call(64), owner("examples")]]
void sumLatency(Input &input) {
    gentest::clobberMemory();
    auto result = sum<T, Reverse>(input.values);
    gentest::doNotOptimizeAway(result);
}

} // namespace measured
