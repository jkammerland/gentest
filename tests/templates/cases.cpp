#include "gentest/runner.h"

// Template matrix test

template <typename T, typename U>
[[using gentest: test("templates/hello"), template(T, int, long), template(U, float, double)]]
void hello() {
    // Just compile; nothing to assert
    gentest::expect(true, "compile");
}

// Parameterized test

[[using gentest: test("templates/params"), parameters(int, 0, 10, 100)]]
void params_test(int i) {
    gentest::expect(i >= 0, "non-negative");
}
