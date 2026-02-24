#include "gentest/assert_libassert.h"
// Bring in mock API (includes generated registry)
#include "../mocking/types.h"
#include "gentest/mock.h"

#include <ostream>

using namespace gentest;

[[using gentest: test("libassert/assert_pass_simple")]]
void assert_pass_simple() {
    ASSERT(1 + 1 == 2);
}

[[using gentest: test("libassert/assert_fail_simple")]]
void assert_fail_simple() {
    ASSERT(1 == 2);
}

[[using gentest: test("libassert/expect_eq_pass")]]
void expect_eq_pass() {
    EXPECT_EQ(3, 3);
}

[[using gentest: test("libassert/expect_eq_fail")]]
void expect_eq_fail() {
    EXPECT_EQ(1, 2);
}

[[using gentest: test("libassert/expect_ne_pass")]]
void expect_ne_pass() {
    EXPECT_NE(1, 2);
}

[[using gentest: test("libassert/assert_pass")]]
void assert_pass() {
    ASSERT(2 == 2);
}

[[using gentest: test("libassert/assert_eq")]]
void assert_eq() {
    ASSERT_EQ(2, 1);
}

struct S {
    int                  x;
    bool                 operator==(const S &rhs) const { return x == rhs.x; }
    friend std::ostream &operator<<(std::ostream &os, const S &s) { return os << "S{x=" << s.x << '}'; }
};

[[using gentest: test("libassert/assert_ne")]]
void assert_ne() {

    S s{1};
    ASSERT_NE(s, s);
}

[[using gentest: test("libassert/assert_fail")]]
void assert_fail() {
    ASSERT(1 == 2);
    // Not reached; the handler throws gentest::assertion to abort the test.
}

[[using gentest: test("libassert/mock_expect_call_pass")]]
void mock_expect_call_pass() {
    gentest::mock<mocking::Calculator> m;
    EXPECT_CALL(m, compute).times(1).returns(123);
    mocking::Calculator *iface = &m;
    EXPECT_EQ(iface->compute(12, 30), 123);
}

[[using gentest: test("libassert/mock_assert_call_pass")]]
void mock_assert_call_pass() {
    gentest::mock<mocking::Ticker> m;
    ASSERT_CALL(m, tick).times(2);
    m.tick(1);
    m.tick(2);
}

// Additional EXPECT samples to exercise boolean path (non-fatal vs fatal separation)
[[using gentest: test("libassert/expect_pass")]]
void expect_pass() {
    EXPECT(1 + 1 == 2);
}

[[using gentest: test("libassert/expect_fail")]]
void expect_fail() {
    EXPECT(false);
}
