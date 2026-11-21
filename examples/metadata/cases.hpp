#pragma once

#include "gentest/test.h"

#include <algorithm>

using namespace gentest::asserts;

namespace metadata {

// Each expanded case inherits the owner, requirement, and custom tag.
[[using gentest: test("bounded_value"), parameters(value, -1, 11), req("LIMIT-001"), owner("examples"), tag("fast")]]
inline void boundedValue(int value) {
    const auto bounded = std::clamp(value, 0, 10);
    EXPECT_TRUE(bounded >= 0 && bounded <= 10);
    gentest::log("checked configured range [0, 10]");
}

// Untagged cases have an empty owner and empty requirements in the inventory.
[[gentest::test("plain")]]
inline void plain() {
    EXPECT_EQ(std::clamp(5, 0, 10), 5);
}

} // namespace metadata
