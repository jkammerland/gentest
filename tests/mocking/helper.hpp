// Helper demonstrating that mocks are usable outside annotated tests.
#pragma once

#include "gentest/mock.h"
#include "types.h"

namespace mocking::helpers {

// Not annotated with [[using gentest: test]] on purpose.
// This compiles in any TU of the test target due to GENTEST_MOCK_* wiring.
[[maybe_unused]] inline int compile_only_usage() {
    // Instantiate a mock and take a pointer to a generated method to
    // force references to the specialization without invoking expectations.
    gentest::mock<Ticker> m;
    (void)m; // silence unused warning
    auto ptr = &gentest::mock<Ticker>::tick;
    (void)ptr;
    return 0;
}

// Ensure ODR-use so compilers don't discard the function body.
[[maybe_unused]] static int _ = compile_only_usage();

} // namespace mocking::helpers
