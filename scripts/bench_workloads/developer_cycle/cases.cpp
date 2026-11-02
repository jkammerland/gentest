#include "cases.hpp"

namespace cycle {

void addition() {
    const auto input = 6 + 8;
    gentest::expect_eq(app::normalizedCents(input), 14);
}

// Added cases belong here.

} // namespace cycle
