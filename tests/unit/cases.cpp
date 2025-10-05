#include "gentest/attributes.h"
#include "gentest/runner.h"
using namespace gentest::asserts;

#include <array>
#include <numeric>
#include <string>

namespace [[using gentest: suite("unit")]] unit {

[[using gentest: test("arithmetic/sum"), category("math"), fast]]
void sum_is_computed() {
    std::array values{1, 2, 3, 4};
    const auto result = std::accumulate(values.begin(), values.end(), 0);
    EXPECT_EQ(result, 10);
}

[[using gentest: test("strings/concatenate"), req("#42"), slow]]
void concatenate_strings() {
    std::string greeting = "hello";
    greeting += " world";
    EXPECT_TRUE(greeting == "hello world");
}

[[using gentest: test("conditions/negate"), linux]]
void negate_condition() {
    bool flag = false;
    EXPECT_TRUE(!flag);
    EXPECT_NE(flag, true);
}

} // namespace unit
