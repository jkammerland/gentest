#include "gentest/attributes.h"
#include "gentest/runner.h"

#include <array>
#include <numeric>
#include <string>

namespace unit
{

GENTEST_TEST_CASE("unit/arithmetic/sum")
void sum_is_computed() {
    std::array values{1, 2, 3, 4};
    const auto result = std::accumulate(values.begin(), values.end(), 0);
    gentest::expect_eq(result, 10, "sum should match");
}

GENTEST_TEST_CASE("unit/strings/concatenate")
void concatenate_strings() {
    std::string greeting = "hello";
    greeting += " world";
    gentest::expect(greeting == "hello world", "string concatenation");
}

GENTEST_TEST_CASE("unit/conditions/negate")
void negate_condition() {
    bool flag = false;
    gentest::expect(!flag, "flag expected to be false");
    gentest::expect_ne(flag, true, "flag should not be true");
}

} // namespace unit
