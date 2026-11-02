#pragma once

#include "gentest/fixture.h"
#include "gentest/test.h"
#include "production.hpp"

#include <vector>

namespace cycle {

struct Cart {
    std::vector<int> prices;
};

// Shared data is read-only, so each case also works in a separate CTest process.
struct [[gentest::fixture(suite)]] Catalog : gentest::FixtureSetup {
    std::vector<int> prices;

    void setUp() override { prices = {10, 20}; }
};

[[gentest::test("fixture_add")]]
void fixtureAdd(Cart &cart, Catalog &catalog);

[[gentest::test("fixture_empty")]]
void fixtureEmpty(Cart &cart, Catalog &catalog);

} // namespace cycle
