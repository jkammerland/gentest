#include "gentest/attributes.h"
#include "gentest/fixture.h"

namespace smoke::fixture_resolution {

struct LocalFixture {
    int value = 0;
};

struct [[using gentest: fixture(suite)]] SharedSuiteFixture : gentest::FixtureSetup {
    void setUp() override {}
};

namespace nested {

[[using gentest: test("smoke/fixture_resolution/local")]]
void local_fixture(LocalFixture &fixture) {
    (void)fixture;
}

[[using gentest: test("smoke/fixture_resolution/shared")]]
void shared_fixture(SharedSuiteFixture &fixture) {
    (void)fixture;
}

} // namespace nested

} // namespace smoke::fixture_resolution
