#include "fixtures.hpp"

namespace cycle {

void fixtureAdd(Cart &cart, Catalog &catalog) {
    gentest::expect_true(cart.prices.empty());
    gentest::require(catalog.prices.size() == 2);

    cart.prices.push_back(app::normalizedCents(catalog.prices.front()));
    gentest::expect_eq(cart.prices.front(), 10);
}

void fixtureEmpty(Cart &cart, Catalog &catalog) {
    gentest::expect_true(cart.prices.empty());
    gentest::expect_eq(catalog.prices.size(), std::size_t{2});
    gentest::expect_eq(app::normalizedCents(200), 100);
}

} // namespace cycle
