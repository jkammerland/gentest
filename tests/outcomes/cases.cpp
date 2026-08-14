#include "cases.hpp"

namespace outcomes {

void runtime_skip_simple() { gentest::skip("runtime condition"); }

} // namespace outcomes

namespace outcomes {

void runtime_skip_if() { gentest::skip_if(true, "conditional runtime condition"); }

} // namespace outcomes

namespace outcomes {

void runtime_skip_prefix_user_text() { gentest::skip("shared fixture unavailable for user-requested skip"); }

} // namespace outcomes

namespace outcomes {

void xfail_expect_fail() {
    gentest::xfail("expected failure");
    EXPECT_TRUE(false, "trigger expected failure");
}

} // namespace outcomes

namespace outcomes {

void xfail_throw() {
    gentest::xfail("expected throw");
    throw std::runtime_error("boom");
}

} // namespace outcomes

namespace outcomes {

void xfail_xpass() { gentest::xfail("unexpected pass"); }

} // namespace outcomes

namespace outcomes {

void skip_overrides_xfail() {
    gentest::xfail("would have been xfail");
    gentest::skip("skip wins");
}

} // namespace outcomes

namespace outcomes {

void skip_after_failure_is_fail() {
    EXPECT_TRUE(false, "failure before skip");
    gentest::skip("should remain failed");
}

} // namespace outcomes
