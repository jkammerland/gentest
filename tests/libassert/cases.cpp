#include "cases.hpp"

void assert_pass_simple() { ASSERT(1 + 1 == 2); }

void assert_fail_simple() { ASSERT(1 == 2); }

void expect_eq_pass() { EXPECT_EQ(3, 3); }

void expect_eq_fail() { EXPECT_EQ(1, 2); }

void expect_ne_pass() { EXPECT_NE(1, 2); }

void assert_pass() { ASSERT(2 == 2); }

void assert_eq() { ASSERT_EQ(2, 1); }

void assert_ne() {

    S s{1};
    ASSERT_NE(s, s);
}

void assert_fail() {
    ASSERT(1 == 2);
    // Not reached; the handler throws gentest::assertion to abort the test.
}

void mock_expect_call_pass() {
    gentest::mock<mocking::Calculator> m;
    EXPECT_CALL(m, compute).times(1).returns(123);
    mocking::Calculator *iface = &m;
    EXPECT_EQ(iface->compute(12, 30), 123);
}

void mock_assert_call_pass() {
    gentest::mock<mocking::Ticker> m;
    ASSERT_CALL(m, tick).times(2);
    m.tick(1);
    m.tick(2);
}

void expect_pass() { EXPECT(1 + 1 == 2); }

void expect_fail() { EXPECT(false); }
