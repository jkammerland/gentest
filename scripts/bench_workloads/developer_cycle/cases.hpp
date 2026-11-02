#pragma once

#include "gentest/test.h"
#include "production.hpp"

#include <vector>

namespace cycle {

[[gentest::test("addition")]]
void addition();

// Rows independently specify the normalization contract at its boundaries.
[[using gentest: test("rows"), parameters_pack((input, expected), (-1, 0), (0, 0), (101, 100))]]
inline void rows(int input, int expected) {
    gentest::expect_eq(app::normalizedCents(input), expected);
}

template <typename T>
[[using gentest: test("values"), template(T, int, long)]]
void values() {
    std::vector<T> values{T{7}, T{8}};
    gentest::expect_eq(values.size(), std::size_t{2});
    gentest::expect_eq(app::normalizedCents(static_cast<int>(values.front())), 7);
}

// Added cases belong here.

} // namespace cycle
