#pragma once

#include "gentest/test.h"

#include <algorithm>

namespace metadata {

// Each expanded case inherits the owner, requirement, and custom tag.
[[using gentest: test("bounded_value"), parameters(value, -1, 11), req("LIMIT-001"), owner("examples"), fast]]
inline void boundedValue(int value) {
    const auto bounded = std::clamp(value, 0, 10);
    gentest::expect_true(bounded >= 0 && bounded <= 10);
    gentest::log("checked configured range [0, 10]");
}

// Untagged cases have an empty owner and empty requirements in the inventory.
[[gentest::test("plain")]]
inline void plain() {
    gentest::expect_eq(std::clamp(5, 0, 10), 5);
}

} // namespace metadata
