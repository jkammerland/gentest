#include <cstdio>

#include "gentest/runner.h"

using namespace gentest::asserts;

namespace regressions::local_teardown_noexceptions {

struct LocalFx : gentest::FixtureSetup, gentest::FixtureTearDown {
    void setUp() override {}

    void tearDown() override {
        std::fputs("local-fixture-teardown-noexc-marker\n", stderr);
        std::fflush(stderr);
    }
};

[[using gentest: test("regressions/local_fixture_teardown_noexceptions/fatal_assert")]]
void fatal_assert(LocalFx &) {
    ASSERT_TRUE(false, "intentional fatal assert in no-exception mode");
}

} // namespace regressions::local_teardown_noexceptions
