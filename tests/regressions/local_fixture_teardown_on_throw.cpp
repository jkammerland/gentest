#include "gentest/runner.h"

namespace regressions::local_teardown {

struct LocalFx : gentest::FixtureSetup, gentest::FixtureTearDown {
    static inline int tear_down_calls = 0;

    void setUp() override {}

    void tearDown() override { ++tear_down_calls; }
};

[[using gentest: test("regressions/local_fixture_teardown/throwing_case")]]
void throwing_case(LocalFx &) {
    // Triggers exception-based early exit from the test body.
    gentest::skip("intentional skip to exercise unwinding");
}

[[using gentest: test("regressions/local_fixture_teardown/verify_teardown_ran")]]
void verify_teardown_ran() {
    gentest::expect_eq(LocalFx::tear_down_calls, 1, "local fixture tearDown must run even when body throws");
}

} // namespace regressions::local_teardown
