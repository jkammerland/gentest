#include "gentest/attributes.h"
#include "gentest/runner.h"
using namespace gentest::asserts;

#include <array>
#include <numeric>
#include <string>

namespace [[using gentest: suite("unit")]] unit {

[[using gentest: test("arithmetic/sum"), group("math"), fast]]
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

[[using gentest: test("approx/absolute")]]
void approx_absolute() {
    using gentest::approx::Approx;
    EXPECT_EQ(3.1415, Approx(3.14).abs(0.01));
    EXPECT_EQ(Approx(10.0).abs(0.5), 10.3);
}

[[using gentest: test("approx/relative")]]
void approx_relative() {
    using gentest::approx::Approx;
    EXPECT_EQ(101.0, Approx(100.0).rel(2.0));   // 1% diff within 2%
    EXPECT_EQ(Approx(200.0).rel(1.0), 198.5);  // 0.75% diff within 1%
}

[[using gentest: test("strings/concatenate"), req("#42"), slow]]
void concatenate_strings() {
    std::string greeting = "hello";
    EXPECT_EQ(greeting.size(), std::size_t{5}, "initial size");
    greeting += " world";
    ASSERT_EQ(greeting.size(), std::size_t{11}, "post-concat size");
    EXPECT_EQ(greeting.substr(0, 5), "hello", "prefix");
    EXPECT_EQ(greeting.substr(6), "world", "suffix");
    EXPECT_TRUE(greeting == "hello world");
}

[[using gentest: test("conditions/negate"), linux]]
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
