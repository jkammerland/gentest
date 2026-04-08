#include "gentest/attributes.h"
#include "gentest/fixture.h"

struct [[using gentest: fixture(suite)]] TopLevelSuiteFixture : gentest::FixtureSetup {};

[[using gentest: test("fixture_errors/top_level_suite_fixture")]]
void top_level_suite_fixture_case(TopLevelSuiteFixture &fixture) {
    (void)fixture;
}
