#pragma once

#include "gentest/runner.h"
using namespace gentest::asserts;

#include <stdexcept>

namespace outcomes {

[[using gentest: test("runtime_skip_simple")]]
void runtime_skip_simple();

[[using gentest: test("runtime_skip_if")]]
void runtime_skip_if();

[[using gentest: test("runtime_skip_prefix_user_text")]]
void runtime_skip_prefix_user_text();

[[using gentest: test("xfail_expect_fail")]]
void xfail_expect_fail();

[[using gentest: test("xfail_throw")]]
void xfail_throw();

[[using gentest: test("xfail_xpass")]]
void xfail_xpass();

[[using gentest: test("skip_overrides_xfail")]]
void skip_overrides_xfail();

[[using gentest: test("skip_after_failure_is_fail")]]
void skip_after_failure_is_fail();

} // namespace outcomes
