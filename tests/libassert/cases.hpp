#pragma once

#include "gentest/assert_libassert.h"
// Bring in mock API (includes generated registry)
#include "public/gentest_textual_suite_mocks.hpp"

#include <ostream>

using namespace gentest;

[[using gentest: test("libassert/assert_pass_simple")]]
void assert_pass_simple();

[[using gentest: test("libassert/assert_fail_simple")]]
void assert_fail_simple();

[[using gentest: test("libassert/expect_eq_pass")]]
void expect_eq_pass();

[[using gentest: test("libassert/expect_eq_fail")]]
void expect_eq_fail();

[[using gentest: test("libassert/expect_ne_pass")]]
void expect_ne_pass();

[[using gentest: test("libassert/assert_pass")]]
void assert_pass();

[[using gentest: test("libassert/assert_eq")]]
void assert_eq();

struct S {
    int                  x;
    bool                 operator==(const S &rhs) const { return x == rhs.x; }
    friend std::ostream &operator<<(std::ostream &os, const S &s) { return os << "S{x=" << s.x << '}'; }
};

[[using gentest: test("libassert/assert_ne")]]
void assert_ne();

[[using gentest: test("libassert/assert_fail")]]
void assert_fail();

[[using gentest: test("libassert/mock_expect_call_pass")]]
void mock_expect_call_pass();

[[using gentest: test("libassert/mock_assert_call_pass")]]
void mock_assert_call_pass();

// Additional EXPECT samples to exercise boolean path (non-fatal vs fatal separation)
[[using gentest: test("libassert/expect_pass")]]
void expect_pass();

[[using gentest: test("libassert/expect_fail")]]
void expect_fail();
