module;

#include "import_only.hpp"

export module gentest.cpp_source_cases;

import gentest;

using namespace gentest::asserts;

export namespace cpp_source {

[[using gentest: test("cpp_source/basic")]]
void basic() {
    EXPECT_EQ(6 * 7, 42);
}

[[using gentest: test("cpp_source/import_only")]]
void importOnly() {
    EXPECT_TRUE(importOnlyProbe());
}

} // namespace cpp_source
