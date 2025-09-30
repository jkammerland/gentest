#include "gentest/attributes.h"
#include "gentest/runner.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-attributes"
#endif

#include <array>
#include <numeric>
#include <string>

namespace unit {

[[using gentest: test("unit/arithmetic/sum"), category("math"), fast]]
void sum_is_computed() {
    std::array values{1, 2, 3, 4};
    const auto result = std::accumulate(values.begin(), values.end(), 0);
    gentest::expect_eq(result, 10, "sum should match");
}

[[using gentest: test("unit/strings/concatenate"), req("#42"), slow]]
void concatenate_strings() {
    std::string greeting = "hello";
    greeting += " world";
    gentest::expect(greeting == "hello world", "string concatenation");
}

[[using gentest: test("unit/conditions/negate"), linux]]
void negate_condition() {
    bool flag = false;
    gentest::expect(!flag, "flag expected to be false");
    gentest::expect_ne(flag, true, "flag should not be true");
}

} // namespace unit

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
