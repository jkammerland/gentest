#pragma once

// A suite authored entirely in a header: the annotated declarations are also
// their own definitions, so the suite needs no authored .cpp of its own.

#include "gentest/runner.h"

#include <string_view>

namespace downstream::header_only {

[[using gentest: test("header_only_test")]]
inline void header_only_test() {
    gentest::expect_true(2 + 2 == 4, "inline header case runs");
}

[[using gentest: test("header_only_second")]]
inline void header_only_second() {
    gentest::expect_true(std::string_view("gentest").starts_with("gen"), "second inline header case runs");
}

} // namespace downstream::header_only
