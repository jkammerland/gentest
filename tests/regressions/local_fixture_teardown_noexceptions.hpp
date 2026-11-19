#pragma once

#include "gentest/runner.h"

#include <cstdio>

namespace regressions::local_teardown_noexceptions {

struct LocalFx : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override {}
    void tearDown() override {
        (void)std::fputs("local-fixture-teardown-noexc-marker\n", stderr);
        (void)std::fflush(stderr);
    }
};

[[using gentest: test("regressions/local_fixture_teardown_noexceptions/fatal_assert")]] void fatal_assert(LocalFx &);

} // namespace regressions::local_teardown_noexceptions

namespace noexceptions {

[[using gentest: test, death]] void fatal_require();
[[using gentest: test, death]] void fatal_fail();
[[using gentest: test, death]] void earlier_expectation();

} // namespace noexceptions
