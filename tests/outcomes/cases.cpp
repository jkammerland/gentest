#include "gentest/runner.h"
using namespace gentest::asserts;

#include <stdexcept>

namespace outcomes {

[[using gentest: test("runtime_skip_simple")]]
void runtime_skip_simple() {
    gentest::skip("runtime condition");
}

[[using gentest: test("runtime_skip_if")]]
void runtime_skip_if() {
    gentest::skip_if(true, "conditional runtime condition");
}

[[using gentest: test("runtime_skip_prefix_user_text")]]
void runtime_skip_prefix_user_text() {
    gentest::skip("shared fixture unavailable for user-requested skip");
}

[[using gentest: test("xfail_expect_fail")]]
void xfail_expect_fail() {
    gentest::xfail("expected failure");
    EXPECT_TRUE(false, "trigger expected failure");
}

[[using gentest: test("xfail_throw")]]
void xfail_throw() {
    gentest::xfail("expected throw");
    throw std::runtime_error("boom");
}

[[using gentest: test("xfail_xpass")]]
void xfail_xpass() {
    gentest::xfail("unexpected pass");
}

[[using gentest: test("skip_overrides_xfail")]]
void skip_overrides_xfail() {
    gentest::xfail("would have been xfail");
    gentest::skip("skip wins");
}

[[using gentest: test("skip_after_failure_is_fail")]]
void skip_after_failure_is_fail() {
    EXPECT_TRUE(false, "failure before skip");
    gentest::skip("should remain failed");
}

} // namespace outcomes
