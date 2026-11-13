// Regression: the exception assertions exist both as statement macros and as
// callable-taking function templates under the same names. Both spellings must
// observe exceptions in include-based code, and a lambda-expression handed to
// the statement macro (which would never be called) must be rejected.
#include "gentest/runner.h"

#include <stdexcept>
#include <vector>

// The macro argument is classified textually; see gentest/assertions.h.
static_assert(gentest::detail::statement_is_uncalled_lambda("[&] { risky(); }"));
static_assert(gentest::detail::statement_is_uncalled_lambda("  [] {}  "));
static_assert(gentest::detail::statement_is_uncalled_lambda("[&]() noexcept(false) -> void { risky(); };"));
static_assert(gentest::detail::statement_is_uncalled_lambda("[ value = 1 ] { use(value); }"));
static_assert(!gentest::detail::statement_is_uncalled_lambda(""));
static_assert(!gentest::detail::statement_is_uncalled_lambda("risky()"));
static_assert(!gentest::detail::statement_is_uncalled_lambda("[&] { risky(); }()"));
static_assert(!gentest::detail::statement_is_uncalled_lambda("first(); second()"));
static_assert(!gentest::detail::statement_is_uncalled_lambda("int value = compute()"));
static_assert(!gentest::detail::statement_is_uncalled_lambda("[[maybe_unused]] Widget widget{}"));
static_assert(!gentest::detail::statement_is_uncalled_lambda("[ [maybe_unused] ] Widget widget{}"));
static_assert(!gentest::detail::statement_is_uncalled_lambda("if (ready) { risky(); }"));
static_assert(!gentest::detail::statement_is_uncalled_lambda("callable"));

namespace {

using namespace gentest::asserts;

int  calls = 0;
int  count() { return ++calls; }
void boom() { throw std::runtime_error("boom"); }

void statement_forms(void *) {
    calls = 0;
    EXPECT_NO_THROW(count());
    EXPECT_NO_THROW(count(); count());
    EXPECT_NO_THROW(const int value = count(); (void)value);
    EXPECT_NO_THROW([[maybe_unused]] const auto value = count());
    EXPECT_NO_THROW([&] { ++calls; }());
    EXPECT_NO_THROW(if (calls > 0) { ++calls; });
    ASSERT_NO_THROW(count());
    ASSERT_NO_THROW(count(); count());
    gentest::require_eq(calls, 10, "every statement form runs exactly once");

    EXPECT_THROW(boom(), std::runtime_error);
    EXPECT_THROW(count(); boom(), std::exception);
    ASSERT_THROW(boom(), std::runtime_error);
    ASSERT_THROW([] { boom(); }(), std::runtime_error);
    gentest::require_eq(calls, 11);
}

void callable_forms(void *) {
    calls = 0;
    (EXPECT_NO_THROW)([&] { ++calls; });
    (ASSERT_NO_THROW)([&] { ++calls; });
    (gentest::asserts::EXPECT_NO_THROW)(count);
    EXPECT_THROW<std::runtime_error>([] { boom(); });
    ASSERT_THROW<std::exception>(boom);
    gentest::require_eq(calls, 3, "every callable is invoked exactly once");
}

void assert_aliases(void *) {
    gentest::assert_true(true);
    gentest::assert_false(false);
    gentest::assert_true(true, "message");
    gentest::assert_false(false, "message");
    gentest::assert_eq(1, 1);
}

// clang-format off
void callable_expect_no_throw(void *) {
#line 2001
    (EXPECT_NO_THROW)([] { throw std::runtime_error("callable expect boom"); });
}
void callable_assert_no_throw(void *) {
#line 2002
    (ASSERT_NO_THROW)([] { throw std::runtime_error("callable assert boom"); });
}
void statement_expect_no_throw(void *) {
#line 2003
    EXPECT_NO_THROW([] { throw std::runtime_error("invoked lambda boom"); }());
}
void callable_expect_throw(void *) {
#line 2004
    EXPECT_THROW<std::logic_error>([] {});
}
void alias_assert_true(void *) {
#line 2005
    gentest::assert_true(false);
}
void alias_assert_false(void *) {
#line 2006
    gentest::assert_false(true);
}
// clang-format on

} // namespace

int main(int argc, char **argv) {
    const gentest::Case cases[]{
        {.name = "throw_forms/passing/statement_forms", .fn = statement_forms, .suite = "throw_forms"},
        {.name = "throw_forms/passing/callable_forms", .fn = callable_forms, .suite = "throw_forms"},
        {.name = "throw_forms/passing/assert_aliases", .fn = assert_aliases, .suite = "throw_forms"},
        {.name = "throw_forms/failing/callable_expect_no_throw", .fn = callable_expect_no_throw, .suite = "throw_forms"},
        {.name = "throw_forms/failing/callable_assert_no_throw", .fn = callable_assert_no_throw, .suite = "throw_forms"},
        {.name = "throw_forms/failing/statement_expect_no_throw", .fn = statement_expect_no_throw, .suite = "throw_forms"},
        {.name = "throw_forms/failing/callable_expect_throw", .fn = callable_expect_throw, .suite = "throw_forms"},
        {.name = "throw_forms/failing/alias_assert_true", .fn = alias_assert_true, .suite = "throw_forms"},
        {.name = "throw_forms/failing/alias_assert_false", .fn = alias_assert_false, .suite = "throw_forms"},
    };
    std::vector<const char *> args(argv, argv + argc);
    return gentest::run_cases(cases, args);
}
