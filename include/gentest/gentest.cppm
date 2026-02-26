module;
#include "gentest/attributes.h"
#include "gentest/bench_util.h"
#include "gentest/fixture.h"
#include "gentest/runner.h"
#include "gentest/runner_fmt.h"

export module gentest;

export namespace gentest {
using ::gentest::Case;
using ::gentest::FixtureLifetime;
using ::gentest::FixtureSetup;
using ::gentest::FixtureTearDown;
using ::gentest::assert_eq;
using ::gentest::assert_false;
using ::gentest::assert_true;
using ::gentest::assertion;
using ::gentest::clear_logs;
using ::gentest::doNotOptimizeAway;
using ::gentest::expect;
using ::gentest::expect_eq;
using ::gentest::expect_false;
using ::gentest::expect_ge;
using ::gentest::expect_gt;
using ::gentest::expect_le;
using ::gentest::expect_lt;
using ::gentest::expect_ne;
using ::gentest::fail;
using ::gentest::failure;
using ::gentest::log;
using ::gentest::log_on_fail;
using ::gentest::logf;
using ::gentest::require;
using ::gentest::require_eq;
using ::gentest::require_false;
using ::gentest::require_ge;
using ::gentest::require_gt;
using ::gentest::require_le;
using ::gentest::require_lt;
using ::gentest::require_ne;
using ::gentest::run_all_tests;
using ::gentest::skip;
using ::gentest::skip_if;
using ::gentest::xfail;
using ::gentest::xfail_if;
}

export namespace gentest::ctx {
using ::gentest::ctx::Adopt;
using ::gentest::ctx::Token;
using ::gentest::ctx::current;
}

export namespace gentest::approx {
using ::gentest::approx::Approx;
}

export namespace gentest::asserts {
using ::gentest::asserts::ASSERT_EQ;
using ::gentest::asserts::ASSERT_FALSE;
using ::gentest::asserts::ASSERT_GE;
using ::gentest::asserts::ASSERT_GT;
using ::gentest::asserts::ASSERT_LE;
using ::gentest::asserts::ASSERT_LT;
using ::gentest::asserts::ASSERT_NE;
using ::gentest::asserts::ASSERT_TRUE;
using ::gentest::asserts::EXPECT_EQ;
using ::gentest::asserts::EXPECT_FALSE;
using ::gentest::asserts::EXPECT_GE;
using ::gentest::asserts::EXPECT_GT;
using ::gentest::asserts::EXPECT_LE;
using ::gentest::asserts::EXPECT_LT;
using ::gentest::asserts::EXPECT_NE;
using ::gentest::asserts::EXPECT_TRUE;
}

export namespace gentest::detail {
using ::gentest::detail::SharedFixtureRegistration;
using ::gentest::detail::SharedFixtureScope;
using ::gentest::detail::get_shared_fixture;
using ::gentest::detail::get_shared_fixture_typed;
using ::gentest::detail::register_cases;
using ::gentest::detail::register_shared_fixture;
using ::gentest::detail::setup_shared_fixtures;
using ::gentest::detail::teardown_shared_fixtures;
}
