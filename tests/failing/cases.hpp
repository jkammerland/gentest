#pragma once

#include "gentest/runner.h"
using namespace gentest::asserts;

#include "public/gentest_textual_suite_mocks.hpp"

#include <memory>
#include <stdexcept>

namespace failing {

struct NullFreeFixture {
    static std::unique_ptr<NullFreeFixture> gentest_allocate() { return {}; }
};

[[using gentest: test("alloc/free_null")]]
void free_null_fixture(NullFreeFixture &);

struct [[using gentest: fixture(suite)]] NullSuiteFixture {
    static std::unique_ptr<NullSuiteFixture>         gentest_allocate() { return {}; }
    [[using gentest: test("alloc/suite_null")]] void t() {}
};

struct [[using gentest: fixture(global)]] NullGlobalFixture {
    static std::unique_ptr<NullGlobalFixture>         gentest_allocate() { return {}; }
    [[using gentest: test("alloc/global_null")]] void t() {}
};

[[using gentest: test("single")]]
void will_fail();

[[using gentest: test("mocking/predicate_mismatch")]]
void predicate_mismatch();

[[using gentest: test("mocking/ambiguous_template_member_pointer")]]
void ambiguous_template_member_pointer();

[[using gentest: test("mocking/nice_exhausted_excess_call")]]
void nice_exhausted_excess_call();

[[using gentest: test("mocking/nice_times_zero_call")]]
void nice_times_zero_call();

[[using gentest: test("mocking/nice_exhausted_fallback_identity")]]
void nice_exhausted_fallback_identity();

[[using gentest: test("logging/attachment")]]
void logging_attachment();

[[using gentest: test("exceptions/expect_throw_location")]]
void expect_throw_location();

[[using gentest: test("exceptions/expect_no_throw_unknown")]]
void expect_no_throw_unknown();

[[using gentest: test("exceptions/assert_throw_location")]]
void assert_throw_location();

[[using gentest: test("exceptions/assert_no_throw_unknown")]]
void assert_no_throw_unknown();

[[using gentest: test("comparison/expect_eq_message_values")]]
void expect_eq_message_values();

[[using gentest: test("comparison/null_function_pointer")]]
void null_function_pointer();

[[using gentest: test("comparison/nonnull_function_pointer")]]
void nonnull_function_pointer();

} // namespace failing
