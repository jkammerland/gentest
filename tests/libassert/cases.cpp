#include "gentest/assert_libassert.h"

using namespace gentest;

// Basic integration smoke tests for libassert handler.

[[using gentest: test("libassert/assert_pass_simple")]]
void assert_pass_simple() { ASSERT(1 + 1 == 2); }

[[using gentest: test("libassert/assert_fail_simple")]]
void assert_fail_simple() { ASSERT(1 == 2); }

[[using gentest: test("libassert/expect_eq_pass")]]
void expect_eq_pass() { EXPECT_EQ(3, 3); }

[[using gentest: test("libassert/expect_eq_fail")]]
void expect_eq_fail() { EXPECT_EQ(1, 2); }

[[using gentest: test("libassert/expect_ne_pass")]]
void expect_ne_pass() { EXPECT_NE(1, 2); }

[[using gentest: test("libassert/assert_pass")]]
void assert_pass() {
    ASSERT(2 == 2);
}

[[using gentest: test("libassert/assert_fail")]]
void assert_fail() {
    ASSERT(1 == 2);
    // Not reached; the handler throws gentest::assertion to abort the test.
}
