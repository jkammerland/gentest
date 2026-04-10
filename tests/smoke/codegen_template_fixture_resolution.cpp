#include "gentest/attributes.h"
#include "gentest/fixture.h"

#include <array>
#include <utility>

namespace smoke::template_fixture_resolution {

using PairValue = std::pair<int, std::array<long, 2>>;

template <typename T> struct LocalFixture {
    T value{};
};

struct [[using gentest: fixture(suite)]] SharedSuiteFixture : gentest::FixtureSetup {
    void setUp() override {}
};

namespace nested {

[[using gentest: test("smoke/template_fixture_resolution/local")]]
void local_fixture(LocalFixture<PairValue> &fixture) {
    (void)fixture;
}

[[using gentest: test("smoke/template_fixture_resolution/shared")]]
void shared_fixture(SharedSuiteFixture &fixture) {
    (void)fixture;
}

} // namespace nested

} // namespace smoke::template_fixture_resolution
