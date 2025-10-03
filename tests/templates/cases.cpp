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

// Multi-argument parameterization across separate blocks

[[using gentest: test("templates/pairs")]]
[[using gentest: parameters(int, 1, 2)]]
[[using gentest: parameters(int, 5, 6)]]
void pairs(int a, int b) {
    gentest::expect(true, "dummy");
}

[[using gentest: test("templates/strs"), parameters(std::string, "a", "b")]]
void strs(std::string s) {
    gentest::expect(!s.empty(), "non-empty");
}
