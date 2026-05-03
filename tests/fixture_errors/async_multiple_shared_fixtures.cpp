#include "gentest/attributes.h"
#include "gentest/runner.h"

namespace fixture_errors::async_multiple_shared {

struct [[using gentest: fixture(suite)]] SuiteFixture {};
struct [[using gentest: fixture(global)]] GlobalFixture {};

[[using gentest: test]]
gentest::async_test<void> depends_on_multiple_shared_fixtures(SuiteFixture &, GlobalFixture &) {
    co_return;
}

} // namespace fixture_errors::async_multiple_shared
