// Regression: EXPECT_THROW/ASSERT_THROW with a std::exception-derived expected
// type must compile and match with RTTI disabled (this TU is built with
// -fno-rtti / /GR-).
#include "gentest/runner.h"

#include <stdexcept>
#include <vector>

namespace {

struct DerivedError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void matches(void *) {
    EXPECT_THROW(throw std::runtime_error("exact"), std::runtime_error);
    EXPECT_THROW(throw DerivedError("derived"), std::runtime_error);
    EXPECT_THROW(throw DerivedError("base"), std::exception);
    EXPECT_THROW(throw gentest::failure("expected failure"), gentest::failure);
    // NOLINTNEXTLINE(bugprone-std-exception-baseclass): non-standard exceptions stay supported.
    EXPECT_THROW(throw 7, int);
    ASSERT_THROW(throw std::runtime_error("exact"), std::runtime_error);
    ASSERT_THROW(throw DerivedError("derived"), DerivedError);
    gentest::asserts::EXPECT_THROW<std::logic_error>([] { throw std::invalid_argument("callable"); });
}

// A gentest::failure is a framework signal: it must keep propagating through an
// EXPECT_THROW that expects one of its base classes.
void failure_propagates(void *) {
    bool propagated = false;
    try {
        EXPECT_THROW(throw gentest::failure("framework signal"), std::exception);
    } catch (const gentest::failure &) { propagated = true; }
    gentest::require(propagated, "gentest::failure must not be swallowed by a base-class expectation");
}

// clang-format off
void expect_mismatch(void *) {
#line 3001
    EXPECT_THROW(throw std::logic_error("expect mismatch"), std::runtime_error);
}
void assert_mismatch(void *) {
#line 3002
    ASSERT_THROW(throw std::logic_error("assert mismatch"), std::runtime_error);
}
// clang-format on

} // namespace

int main(int argc, char **argv) {
    const gentest::Case cases[]{
        {.name = "no_rtti/passing/matches", .fn = matches, .suite = "no_rtti"},
        {.name = "no_rtti/passing/failure_propagates", .fn = failure_propagates, .suite = "no_rtti"},
        {.name = "no_rtti/failing/expect_mismatch", .fn = expect_mismatch, .suite = "no_rtti"},
        {.name = "no_rtti/failing/assert_mismatch", .fn = assert_mismatch, .suite = "no_rtti"},
    };
    std::vector<const char *> args(argv, argv + argc);
    return gentest::run_cases(cases, args);
}
