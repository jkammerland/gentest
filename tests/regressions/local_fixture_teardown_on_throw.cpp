#include <cstdlib>
#include <stdexcept>

#include "gentest/runner.h"

namespace regressions::local_teardown {

struct BodySkipFx : gentest::FixtureSetup, gentest::FixtureTearDown {
    bool setup_complete    = false;
    bool teardown_complete = false;

    void setUp() override { setup_complete = true; }

    void tearDown() override { teardown_complete = true; }

    ~BodySkipFx() override {
        if (setup_complete && !teardown_complete) {
            std::abort();
        }
    }
};

[[using gentest: test("regressions/local_fixture_teardown/throwing_case")]]
void throwing_case(BodySkipFx &) {
    // Triggers exception-based early exit from the test body.
    gentest::skip("intentional skip to exercise unwinding");
}

struct SetupThrowProbeFx : gentest::FixtureSetup, gentest::FixtureTearDown {
    bool setup_complete    = false;
    bool teardown_complete = false;

    void setUp() override { setup_complete = true; }

    void tearDown() override { teardown_complete = true; }

    ~SetupThrowProbeFx() override {
        if (setup_complete && !teardown_complete) {
            std::abort();
        }
    }
};

struct SetupThrowFx : gentest::FixtureSetup {
    void setUp() override { throw std::runtime_error("intentional setup throw to exercise unwinding"); }
};

[[using gentest: test("regressions/local_fixture_teardown/setup_throw_case")]]
void setup_throw_case(SetupThrowProbeFx &, SetupThrowFx &) {}

struct SetupSkipProbeFx : gentest::FixtureSetup, gentest::FixtureTearDown {
    bool setup_complete    = false;
    bool teardown_complete = false;

    void setUp() override { setup_complete = true; }

    void tearDown() override { teardown_complete = true; }

    ~SetupSkipProbeFx() override {
        if (setup_complete && !teardown_complete) {
            std::abort();
        }
    }
};

struct SetupSkipFx : gentest::FixtureSetup {
    void setUp() override { gentest::skip("intentional setup skip to exercise unwinding"); }
};

[[using gentest: test("regressions/local_fixture_teardown/setup_skip_case")]]
void setup_skip_case(SetupSkipProbeFx &, SetupSkipFx &) {}

} // namespace regressions::local_teardown
