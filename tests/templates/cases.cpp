#include "gentest/runner.h"
#include <chrono>

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

[[using gentest: test("templates/strs"), parameters(std::string, "a", b)]]
void strs(std::string s) {
    gentest::expect(!s.empty(), "non-empty");
}

// Mixed axes and templates

template <typename T>
[[using gentest: test("templates/bar"), template(T, int, long), parameters(std::string, x, y)]]
void bar(std::string) {
    gentest::expect(true, "ok");
}

// parameters_pack: bundle multiple args per row

[[using gentest: test("templates/pack"), parameters_pack((int, string), (42, a), (7, "b"))]]
void pack(int a, std::string b) {
    gentest::expect(true, "ok");
}

// Raw axis: verbatim expressions

template <typename T>
[[using gentest: test("templates/raw"), parameters(raw, 1+2, std::chrono::milliseconds{10})]]
void raw_any(T v) {
    (void)v;
    gentest::expect(true, "ok");
}

// Char-like literals

[[using gentest: test("templates/chars"), parameters(char, a, 'z')]]
void chars(char c) {
    (void)c;
    gentest::expect(true, "ok");
}

// Wide/UTF strings

[[using gentest: test("templates/wstrs"), parameters(std::wstring, Alpha)]]
void wstrs(std::wstring s) {
    (void)s;
    gentest::expect(true, "ok");
}
