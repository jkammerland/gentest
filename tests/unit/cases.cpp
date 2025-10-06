#include "gentest/attributes.h"
#include "gentest/runner.h"
using namespace gentest::asserts;

#include <array>
#include <numeric>
#include <string>

namespace unit {

[[using gentest: test("unit/arithmetic/sum"), category("math"), fast]]
void sum_is_computed() {
    std::array values{1, 2, 3, 4};
    const auto result = std::accumulate(values.begin(), values.end(), 0);
    EXPECT_EQ(values.size(), std::size_t{4});
    ASSERT_EQ(values.front(), 1, "first element");
    EXPECT_EQ(values.back(), 4, "last element");
    const auto average = static_cast<double>(result) / values.size();
    EXPECT_EQ(result, 10);
    EXPECT_EQ(average, 2.5, "arithmetic mean");
}

[[using gentest: test("unit/strings/concatenate"), req("#42"), slow]]
void concatenate_strings() {
    std::string greeting = "hello";
    EXPECT_EQ(greeting.size(), std::size_t{5}, "initial size");
    greeting += " world";
    ASSERT_EQ(greeting.size(), std::size_t{11}, "post-concat size");
    EXPECT_EQ(greeting.substr(0, 5), "hello", "prefix");
    EXPECT_EQ(greeting.substr(6), "world", "suffix");
    EXPECT_TRUE(greeting == "hello world");
}

[[using gentest: test("unit/conditions/negate"), linux]]
void negate_condition() {
    bool flag = false;
    ASSERT_EQ(flag, false, "starts false");
    EXPECT_TRUE(!flag);
    EXPECT_NE(flag, true);

    flag = !flag;
    ASSERT_TRUE(flag, "negation flips to true");
    EXPECT_EQ(flag, true, "flag now true");

    flag = !flag;
    EXPECT_TRUE(!flag);
    EXPECT_EQ(flag, false, "double negation");
}

} // namespace unit
