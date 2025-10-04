#include "gentest/runner.h"
#include <chrono>
#include <type_traits>

// Template matrix test

template <typename T, typename U>
[[using gentest: test("templates/hello"), template(T, int, long), template(U, float, double)]]
void hello() {
    // Validate template kinds with compile-time checks
    if constexpr (!std::is_integral_v<T>) {
        gentest::expect(false, "T must be integral");
    } else if constexpr (!std::is_floating_point_v<U>) {
        gentest::expect(false, "U must be floating point");
    } else {
        gentest::expect(true, "template type checks passed");
    }
}

// Parameterized test

[[using gentest: test("templates/params"), parameters(int, 0, 10, 100)]]
void params_test(int i) {
    gentest::expect(i == 0 || i == 10 || i == 100, "value in set {0,10,100}");
}

// Multi-argument parameterization across separate blocks

[[using gentest: test("templates/pairs")]]
[[using gentest: parameters(int, 1, 2)]]
[[using gentest: parameters(int, 5, 6)]]
void pairs(int a, int b) {
    gentest::expect((a == 1 || a == 2) && (b == 5 || b == 6), "cartesian pairs valid");
}

[[using gentest: test("templates/strs"), parameters(std::string, "a", b)]]
void strs(std::string s) {
    gentest::expect(s == "a" || s == "b", "strings axis values");
}

// Mixed axes and templates

template <typename T>
[[using gentest: test("templates/bar"), template(T, int, long), parameters(std::string, x, y)]]
void bar(std::string s) {
    if constexpr (!std::is_integral_v<T>) {
        gentest::expect(false, "T must be integral");
    } else {
        gentest::expect(s == "x" || s == "y", "string axis values");
    }
}

// parameters_pack: bundle multiple args per row

[[using gentest: test("templates/pack"), parameters_pack((int, string), (42, a), (7, "b"))]]
void pack(int a, std::string b) {
    const bool row1 = (a == 42 && b == "a");
    const bool row2 = (a == 7 && b == "b");
    gentest::expect(row1 || row2, "parameters_pack rows valid");
}

// Raw axis: verbatim expressions

[[using gentest: test("templates/raw"), parameters(raw, std::chrono::milliseconds{10})]]
void raw_msec(std::chrono::milliseconds v) {
    gentest::expect_eq(v.count(), 10LL, "raw milliseconds value");
}

// Char-like literals

[[using gentest: test("templates/chars"), parameters(char, a, 'z')]]
void chars(char c) {
    gentest::expect(c == 'a' || c == 'z', "char axis values");
}

// Wide/UTF strings

[[using gentest: test("templates/wstrs"), parameters(std::wstring, Alpha)]]
void wstrs(std::wstring s) {
    gentest::expect(s == L"Alpha", "wide string literal value");
}

// Typed + parameter validation using if constexpr over T
template <typename T>
[[using gentest: test("templates/typed_values"), template(T, int, long), parameters(int, 2, 4)]]
void typed_values(int v) {
    if constexpr (std::is_same_v<T, int>) {
        gentest::expect(v == 2 || v == 4, "int axis values");
    } else if constexpr (std::is_same_v<T, long>) {
        gentest::expect(v == 2 || v == 4, "long axis values");
    } else {
        gentest::expect(false, "unexpected T");
    }
}

// NTTP (non-type template parameter) validation
template <typename T, int N>
[[using gentest: test("templates/nttp"), template(T, int), template(NTTP: N, 1, 2)]]
void nttp() {
    if constexpr (!std::is_same_v<T, int>) {
        gentest::expect(false, "T must be int for this test");
    } else {
        gentest::expect(N == 1 || N == 2, "NTTP N in {1,2}");
    }
}
