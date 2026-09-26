#pragma once

#include "gentest/fixture.h"
#include "gentest/test.h"

#include <vector>

namespace fixtures {

// An unannotated fixture parameter gets a fresh instance for each case.
struct Cart {
    std::vector<int> prices;
};

// Shared setup belongs in the common ancestor namespace of its consumers.
struct [[gentest::fixture(suite)]] Catalog : gentest::FixtureSetup, gentest::FixtureTearDown {
    std::vector<int> prices;

    void setUp() override { prices = {10, 20}; }
    void tearDown() override { prices.clear(); }
};

[[gentest::test("fresh_cart")]]
inline void freshCart(Cart &cart) {
    gentest::expect_true(cart.prices.empty());
    cart.prices.push_back(99);
}

namespace checkout {

[[gentest::test("add_item")]]
inline void addItem(Cart &cart, Catalog &catalog) {
    gentest::expect_true(cart.prices.empty());
    gentest::asserts::ASSERT_EQ(catalog.prices.size(), std::size_t{2});
    cart.prices.push_back(catalog.prices.front());
    gentest::expect_eq(cart.prices.front(), 10);
}

[[gentest::test("empty_cart")]]
inline void emptyCart(Cart &cart, Catalog &catalog) {
    gentest::expect_true(cart.prices.empty());
    gentest::expect_eq(catalog.prices.size(), std::size_t{2});
}

} // namespace checkout
} // namespace fixtures
