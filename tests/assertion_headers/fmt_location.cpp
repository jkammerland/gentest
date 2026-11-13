// Regression: the fmt-message assertion overloads must be callable with
// formatting arguments and must report failures at the caller's location.
// `#line` pins the reported lines so the CTest expectations stay stable.
#include "gentest/runner_fmt.h"

#include <source_location>
#include <string>
#include <vector>

namespace {

using namespace gentest::asserts;

void passing(void *) {
    const int value = 1;
    gentest::expect(value == 1, "value {}", value);
    gentest::expect_true(value == 1, "value {}", value);
    gentest::expect_eq(value, 1, "value {}", value);
    gentest::expect_ne(value, 2, "value {}", value);
    gentest::require(value == 1, "value {}", value);
    gentest::require_eq(value, 1, "value {}", value);
    gentest::require_ne(value, 2, "value {} {}", value, "text");
    EXPECT_TRUE(value == 1, "value {}", value);
    EXPECT_EQ(value, 1, "value {}", value);
    EXPECT_NE(value, 2, "value {}", value);
    ASSERT_TRUE(value == 1, "value {}", value);
    ASSERT_EQ(value, 1, "value {}", value);
    ASSERT_NE(value, 2, "value {}", value);

    // Runtime format strings stay accepted.
    const std::string runtime_format = "value {}";
    gentest::expect(value == 1, fmt::runtime(runtime_format), value);
    EXPECT_EQ(value, 1, fmt::runtime(runtime_format), value);

    // The plain message overloads keep taking an explicit location.
    const auto here = std::source_location::current();
    gentest::expect(value == 1, "plain", here);
    gentest::expect_eq(value, 1, "plain", here);
    gentest::require_eq(value, 1, "plain", here);
    EXPECT_TRUE(value == 1, "plain", here);
    EXPECT_EQ(value, 1, "plain", here);
    ASSERT_NE(value, 2, "plain", here);
}

// clang-format off
void lower_expect(void *) {
#line 1001
    gentest::expect(false, "lower expect {}", 1);
}
void lower_expect_true(void *) {
#line 1002
    gentest::expect_true(false, "lower expect_true {}", 2);
}
void lower_expect_eq(void *) {
#line 1003
    gentest::expect_eq(1, 2, "lower expect_eq {}", 3);
}
void lower_expect_ne(void *) {
#line 1004
    gentest::expect_ne(1, 1, "lower expect_ne {}", 4);
}
void lower_require(void *) {
#line 1005
    gentest::require(false, "lower require {}", 5);
}
void lower_require_eq(void *) {
#line 1006
    gentest::require_eq(1, 2, "lower require_eq {}", 6);
}
void lower_require_ne(void *) {
#line 1007
    gentest::require_ne(1, 1, "lower require_ne {}", 7);
}
void upper_expect_true(void *) {
#line 1008
    EXPECT_TRUE(false, "upper EXPECT_TRUE {}", 8);
}
void upper_expect_eq(void *) {
#line 1009
    EXPECT_EQ(1, 2, "upper EXPECT_EQ {}", 9);
}
void upper_expect_ne(void *) {
#line 1010
    EXPECT_NE(1, 1, "upper EXPECT_NE {}", 10);
}
void upper_assert_true(void *) {
#line 1011
    ASSERT_TRUE(false, "upper ASSERT_TRUE {}", 11);
}
void upper_assert_eq(void *) {
#line 1012
    ASSERT_EQ(1, 2, "upper ASSERT_EQ {}", 12);
}
void upper_assert_ne(void *) {
#line 1013
    ASSERT_NE(1, 1, "upper ASSERT_NE {}", 13);
}
void runtime_format(void *) {
    const std::string format = "runtime format {}";
#line 1014
    EXPECT_TRUE(false, fmt::runtime(format), 14);
}
// clang-format on

} // namespace

int main(int argc, char **argv) {
    const gentest::Case cases[]{
        {.name = "fmt_location/passing", .fn = passing, .suite = "fmt_location"},
        {.name = "fmt_location/failing/lower_expect", .fn = lower_expect, .suite = "fmt_location"},
        {.name = "fmt_location/failing/lower_expect_true", .fn = lower_expect_true, .suite = "fmt_location"},
        {.name = "fmt_location/failing/lower_expect_eq", .fn = lower_expect_eq, .suite = "fmt_location"},
        {.name = "fmt_location/failing/lower_expect_ne", .fn = lower_expect_ne, .suite = "fmt_location"},
        {.name = "fmt_location/failing/lower_require", .fn = lower_require, .suite = "fmt_location"},
        {.name = "fmt_location/failing/lower_require_eq", .fn = lower_require_eq, .suite = "fmt_location"},
        {.name = "fmt_location/failing/lower_require_ne", .fn = lower_require_ne, .suite = "fmt_location"},
        {.name = "fmt_location/failing/upper_expect_true", .fn = upper_expect_true, .suite = "fmt_location"},
        {.name = "fmt_location/failing/upper_expect_eq", .fn = upper_expect_eq, .suite = "fmt_location"},
        {.name = "fmt_location/failing/upper_expect_ne", .fn = upper_expect_ne, .suite = "fmt_location"},
        {.name = "fmt_location/failing/upper_assert_true", .fn = upper_assert_true, .suite = "fmt_location"},
        {.name = "fmt_location/failing/upper_assert_eq", .fn = upper_assert_eq, .suite = "fmt_location"},
        {.name = "fmt_location/failing/upper_assert_ne", .fn = upper_assert_ne, .suite = "fmt_location"},
        {.name = "fmt_location/failing/runtime_format", .fn = runtime_format, .suite = "fmt_location"},
    };
    std::vector<const char *> args(argv, argv + argc);
    return gentest::run_cases(cases, args);
}
