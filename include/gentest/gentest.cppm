module;

#include "gentest/runner.h"

export module gentest;

export namespace gentest {

using ::gentest::run_all_tests;
using ::gentest::Case;
using ::gentest::FixtureLifetime;
using ::gentest::FixtureSetup;
using ::gentest::FixtureTearDown;
using ::gentest::log;
using ::gentest::log_on_fail;
using ::gentest::skip;
using ::gentest::skip_if;
using ::gentest::xfail;
using ::gentest::xfail_if;

namespace approx {
using ::gentest::approx::Approx;
}

namespace ctx {
using ::gentest::ctx::Adopt;
using ::gentest::ctx::Token;
using ::gentest::ctx::current;
}

namespace detail {
using ::gentest::detail::BenchPhase;
using ::gentest::detail::FixtureHandle;
using ::gentest::detail::NoExceptionsFatalHookScope;
using ::gentest::detail::SharedFixtureScope;
using ::gentest::detail::bench_phase;
using ::gentest::detail::get_shared_fixture_typed;
using ::gentest::detail::record_bench_error;
using ::gentest::detail::record_failure;
using ::gentest::detail::register_cases;
using ::gentest::detail::register_shared_fixture;
using ::gentest::detail::skip_shared_fixture_unavailable;
using ::gentest::detail::exceptions_enabled;
}

namespace asserts {
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

} // namespace gentest
