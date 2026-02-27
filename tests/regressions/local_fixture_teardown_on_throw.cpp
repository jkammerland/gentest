#include <stdexcept>

#include "gentest/runner.h"

namespace regressions::local_teardown {

struct BodySkipFx : gentest::FixtureSetup, gentest::FixtureTearDown {
    static inline int tear_down_calls = 0;

    void setUp() override {}

    void tearDown() override { ++tear_down_calls; }
};

[[using gentest: test("regressions/local_fixture_teardown/throwing_case")]]
void throwing_case(BodySkipFx &) {
    // Triggers exception-based early exit from the test body.
    gentest::skip("intentional skip to exercise unwinding");
}

[[using gentest: test("regressions/local_fixture_teardown/verify_teardown_ran")]]
void verify_teardown_ran() {
    gentest::expect_eq(BodySkipFx::tear_down_calls, 1, "local fixture tearDown must run even when body throws");
}

struct SetupThrowProbeFx : gentest::FixtureSetup, gentest::FixtureTearDown {
    static inline int tear_down_calls = 0;

    void setUp() override {}

    void tearDown() override { ++tear_down_calls; }
};

struct SetupThrowFx : gentest::FixtureSetup {
    void setUp() override { throw std::runtime_error("intentional setup throw to exercise unwinding"); }
};

[[using gentest: test("regressions/local_fixture_teardown/setup_throw_case")]]
void setup_throw_case(SetupThrowProbeFx &, SetupThrowFx &) {}

[[using gentest: test("regressions/local_fixture_teardown/verify_setup_throw_teardown_ran")]]
void verify_setup_throw_teardown_ran() {
    gentest::expect_eq(SetupThrowProbeFx::tear_down_calls, 1, "local teardown must run when a later setup throws");
}

struct SetupSkipProbeFx : gentest::FixtureSetup, gentest::FixtureTearDown {
    static inline int tear_down_calls = 0;

    void setUp() override {}

    void tearDown() override { ++tear_down_calls; }
};

struct SetupSkipFx : gentest::FixtureSetup {
    void setUp() override { gentest::skip("intentional setup skip to exercise unwinding"); }
};

[[using gentest: test("regressions/local_fixture_teardown/setup_skip_case")]]
void setup_skip_case(SetupSkipProbeFx &, SetupSkipFx &) {}

[[using gentest: test("regressions/local_fixture_teardown/verify_setup_skip_teardown_ran")]]
void verify_setup_skip_teardown_ran() {
    gentest::expect_eq(SetupSkipProbeFx::tear_down_calls, 1, "local teardown must run when a later setup skips");
}

} // namespace regressions::local_teardown
