// Helper demonstrating that mocks are usable outside annotated tests.
#pragma once

#include "public/gentest_textual_suite_mocks.hpp"

namespace mocking::helpers {

// Not annotated with [[using gentest: test]] on purpose.
// This compiles in any TU of the test target through the explicit mock surface.
[[maybe_unused]] inline int compile_only_usage() {
    // Instantiate a mock and take a pointer to a generated method to
    // force references to the specialization without invoking expectations.
    gentest::mock<Ticker> m;
    (void)m; // silence unused warning
    auto ptr = &gentest::mock<Ticker>::tick;
    (void)ptr;
    return 0;
}

// Keep a compile-time reference without adding a potentially throwing static initializer.
[[maybe_unused]] inline constexpr auto compile_only_usage_anchor = &compile_only_usage;

} // namespace mocking::helpers
