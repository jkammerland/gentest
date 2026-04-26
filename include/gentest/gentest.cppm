module;

#include "gentest/runner.h"

#ifdef EXPECT_THROW
#undef EXPECT_THROW
#endif
#ifdef EXPECT_NO_THROW
#undef EXPECT_NO_THROW
#endif
#ifdef ASSERT_THROW
#undef ASSERT_THROW
#endif
#ifdef ASSERT_NO_THROW
#undef ASSERT_NO_THROW
#endif

export module gentest;

export namespace gentest {

using ::gentest::Adoption;
using ::gentest::assert_eq;
using ::gentest::assert_false;
using ::gentest::assert_true;
using ::gentest::assertion;
using ::gentest::async_test;
using ::gentest::AsyncFixtureSetup;
using ::gentest::AsyncFixtureTearDown;
using ::gentest::Case;
using ::gentest::CurrentContext;
using ::gentest::CurrentToken;
using ::gentest::expect;
using ::gentest::expect_eq;
using ::gentest::expect_false;
using ::gentest::expect_ge;
using ::gentest::expect_gt;
using ::gentest::expect_le;
using ::gentest::expect_lt;
using ::gentest::expect_ne;
using ::gentest::expect_true;
using ::gentest::fail;
using ::gentest::failure;
using ::gentest::FixtureLifetime;
using ::gentest::FixtureSetup;
using ::gentest::FixtureTearDown;
using ::gentest::get_current_context;
using ::gentest::get_current_token;
using ::gentest::log;
using ::gentest::LogPolicy;
using ::gentest::operator|;
using ::gentest::operator|=;
using ::gentest::registered_cases;
using ::gentest::require;
using ::gentest::require_eq;
using ::gentest::require_false;
using ::gentest::require_ge;
using ::gentest::require_gt;
using ::gentest::require_le;
using ::gentest::require_lt;
using ::gentest::require_ne;
using ::gentest::run_all_tests;
using ::gentest::set_current_context;
using ::gentest::set_current_token;
using ::gentest::set_default_log_policy;
using ::gentest::set_log_policy;
using ::gentest::skip;
using ::gentest::skip_if;
using ::gentest::to_underlying;
using ::gentest::xfail;
using ::gentest::xfail_if;

namespace approx {
using ::gentest::approx::Approx;
using ::gentest::approx::operator==;
using ::gentest::approx::operator!=;
} // namespace approx

namespace async {
using ::gentest::async::completion_source;
using ::gentest::async::manual_event;
using ::gentest::async::yield;
} // namespace async

namespace asserts {
using ::gentest::asserts::ASSERT_EQ;
using ::gentest::asserts::ASSERT_FALSE;
using ::gentest::asserts::ASSERT_GE;
using ::gentest::asserts::ASSERT_GT;
using ::gentest::asserts::ASSERT_LE;
using ::gentest::asserts::ASSERT_LT;
using ::gentest::asserts::ASSERT_NE;
using ::gentest::asserts::ASSERT_NO_THROW;
using ::gentest::asserts::ASSERT_THROW;
using ::gentest::asserts::ASSERT_TRUE;
using ::gentest::asserts::EXPECT_EQ;
using ::gentest::asserts::EXPECT_FALSE;
using ::gentest::asserts::EXPECT_GE;
using ::gentest::asserts::EXPECT_GT;
using ::gentest::asserts::EXPECT_LE;
using ::gentest::asserts::EXPECT_LT;
using ::gentest::asserts::EXPECT_NE;
using ::gentest::asserts::EXPECT_NO_THROW;
using ::gentest::asserts::EXPECT_THROW;
using ::gentest::asserts::EXPECT_TRUE;
} // namespace asserts

} // namespace gentest
