#include "values.hpp"

#include "gentest/bench_util.h"
#include "gentest/test.h"

#include <array>
#include <cmath>
#include <limits>

#if defined(_MSC_VER)
#define GENTEST_VALUE_NOINLINE __declspec(noinline)
#else
#define GENTEST_VALUE_NOINLINE __attribute__((noinline))
#endif

namespace bench_util_tests {
namespace {

// Keeping these bodies out of line exposes lost stores even when the caller
// knows the expected value. An empty compiler barrier must not change bits.
template <class T> GENTEST_VALUE_NOINLINE T constant_value() {
    T value = static_cast<T>(12345.6789L);
    gentest::doNotOptimizeAway(value);
    return value;
}

template <class T> GENTEST_VALUE_NOINLINE T runtime_value(T value) {
    gentest::doNotOptimizeAway(value);
    return value;
}

} // namespace

template <class T> void floating_preservation() {
    using namespace gentest::asserts;
    EXPECT_TRUE(constant_value<T>() == static_cast<T>(12345.6789L));

    // Exact equality plus sign checks protect zero, subnormal and finite values;
    // NaN is checked separately because equality is false by definition.
    const std::array values{T{0},
                            -T{0},
                            T{1.25},
                            T{-9.5},
                            std::numeric_limits<T>::min(),
                            std::numeric_limits<T>::denorm_min(),
                            std::numeric_limits<T>::max(),
                            std::numeric_limits<T>::lowest(),
                            std::numeric_limits<T>::infinity()};
    for (const T expected : values) {
        const T actual = runtime_value(expected);
        EXPECT_TRUE(actual == expected);
        EXPECT_EQ(std::signbit(actual), std::signbit(expected));
    }
    EXPECT_TRUE(std::isnan(runtime_value(std::numeric_limits<T>::quiet_NaN())));
}

template void floating_preservation<float>();
template void floating_preservation<double>();
template void floating_preservation<long double>();

} // namespace bench_util_tests
