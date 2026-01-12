#pragma once

#include "gentest/runner.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

namespace templates {

// Template matrix test

template <typename T, typename U>
[[using gentest: test("hello"), template(T, int, long), template(U, float, double)]]
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

// Mixed axes and templates

template <typename T>
[[using gentest: test("bar"), template(T, int, long), parameters(s, x, y)]]
void bar(std::string s) {
    if constexpr (!std::is_integral_v<T>) {
        gentest::expect(false, "T must be integral");
    } else {
        gentest::expect(s == "x" || s == "y", "string axis values");
    }
}

// Typed + parameter validation using if constexpr over T

template <typename T>
[[using gentest: test("typed_values"), template(T, int, long), parameters(v, 2, 4)]]
void typed_values(int v) {
    if constexpr (std::is_same_v<T, int>) {
        gentest::expect(v == 2 || v == 4, "int axis values");
    } else if constexpr (std::is_same_v<T, long>) {
        gentest::expect(v == 2 || v == 4, "long axis values");
    } else {
        gentest::expect(false, "unexpected T");
    }
}

// Value template parameter validation

template <typename T, int N>
[[using gentest: test("nttp"), template(T, int), template(N, 1, 2)]]
void nttp() {
    if constexpr (!std::is_same_v<T, int>) {
        gentest::expect(false, "T must be int for this test");
    } else {
        gentest::expect(N == 1 || N == 2, "N in {1,2}");
    }
}

// Interleaved template parameters (value then type); validate both

template <int N, typename T>
[[using gentest: test("interleaved"), template(N, 1, 2), template(T, int, long)]]
void interleaved() {
    if constexpr (!std::is_integral_v<T>) {
        gentest::expect(false, "T must be integral");
    } else {
        gentest::expect(N == 1 || N == 2, "N in {1,2}");
    }
}

// Three type parameters; small matrix to exercise expansion of >2 templates

template <typename T, typename U, typename V>
[[using gentest: test("triad"), template(T, int, long), template(U, float), template(V, char)]]
void triad() {
    if constexpr (!std::is_integral_v<T>) {
        gentest::expect(false, "T integral");
    } else if constexpr (!std::is_floating_point_v<U>) {
        gentest::expect(false, "U floating");
    } else if constexpr (!std::is_integral_v<V>) {
        gentest::expect(false, "V integral-ish");
    } else {
        gentest::expect(true, "triad ok");
    }
}

// Two value template parameters only; ensure cross product expands correctly and values are visible

template <int A, int B>
[[using gentest: test("nttp_pair"), template(A, 1, 2), template(B, 5)]]
void nttp_pair() {
    gentest::expect((A == 1 || A == 2) && B == 5, "pair values");
}

// Interleaved with three params: type, value, value

template <typename A, int N, int M>
[[using gentest: test("interleaved2"), template(A, long), template(M, 3, 4), template(N, 1)]]
void interleaved2() {
    if constexpr (!std::is_same_v<A, long>) {
        gentest::expect(false, "A must be long");
    } else {
        gentest::expect((N == 1) && (M == 3 || M == 4), "N==1 and M in {3,4}");
    }
}

// Triad with interleaving: value, type, type

template <int N, typename T, typename U>
[[using gentest: test("triad_interleaved"), template(T, int, long), template(N, 7, 8), template(U, double)]]
void triad_interleaved() {
    if constexpr (!std::is_integral_v<T> || !std::is_floating_point_v<U>) {
        gentest::expect(false, "type checks");
    } else {
        gentest::expect(N == 7 || N == 8, "N in {7,8}");
    }
}

// Boolean value template parameter

template <bool B>
[[using gentest: test("nttp_bool"), template(B, true, false)]]
void nttp_bool() {
    if constexpr (B) {
        gentest::expect(true, "B==true path");
    } else {
        gentest::expect(true, "B==false path");
    }
}

// size_t value template parameter

template <std::size_t N>
[[using gentest: test("size_value"), template(N, 16, 32)]]
void size_value() {
    gentest::expect(N == 16 || N == 32, "N in {16,32}");
}

// Scoped enum in nested namespaces; value template parameter should accept fully qualified tokens

namespace ns_outer {
namespace ns_inner {
enum class Shade { Dark, Light };
}
} // namespace ns_outer

template <ns_outer::ns_inner::Shade S>
[[using gentest: test("enum_value_scoped"),
  template(S, templates::ns_outer::ns_inner::Shade::Dark, templates::ns_outer::ns_inner::Shade::Light)]]
void enum_value_scoped() {
    using ns_outer::ns_inner::Shade;
    gentest::expect(S == Shade::Dark || S == Shade::Light, "S in {Dark,Light}");
}

// Mixed type + value template + runtime axes (unified template syntax)

template <typename T, std::size_t N>
[[using gentest: test("mix/type_nttp_value"), template(T, int), template(N, 16), parameters(v, 3)]]
void mix_type_nttp_value(int v) {
    if constexpr (!std::is_same_v<T, int>) {
        gentest::expect(false, "T must be int");
    } else {
        gentest::expect(N == 16 && v == 3, "N==16 and v==3");
    }
}

// Value template-only mix with different kinds

template <std::size_t N, bool B>
[[using gentest: test("mix/nttp_bool_mix"), template(N, 4), template(B, true)]]
void mix_nttp_bool_mix() {
    gentest::expect(N == 4 && B == true, "N==4 and B==true");
}

// 2x1x2 matrix: two type axes (sizes 2 and 1) and one value parameter axis (size 2)

template <typename T, typename U, int N>
[[using gentest: test("mix/2x1x2"), template(T, int, long), template(U, float), template(N, 5, 9)]]
void mix_2x1x2() {
    if constexpr (!std::is_integral_v<T>) {
        gentest::expect(false, "T must be integral");
    } else if constexpr (!std::is_floating_point_v<U>) {
        gentest::expect(false, "U must be floating point");
    } else {
        gentest::expect(N == 5 || N == 9, "N in {5,9}");
    }
}

} // namespace templates

// Enum value template parameter

enum class Color { Red, Green, Blue };

template <Color C>
[[using gentest: test("enum_value"), template(C, Color::Red, Color::Blue)]]
void enum_value() {
    gentest::expect(C == Color::Red || C == Color::Blue, "C in {Red,Blue}");
}

// Local struct used by non-template parameterized tests

struct LocalPoint {
    int x;
    int y;
};

// Multiple [[...]] blocks: mix of templates and parameters split

template <typename T, int N>
[[using gentest: test("multi_blocks/mixed_split")]] [[using gentest: template(T, int)]] [[using gentest: template(
    N, 7)]] [[using gentest: parameters(s, Hello, "World")]]
void multi_mixed_split(const std::string &s) {
    if constexpr (!std::is_same_v<T, int> || N != 7) {
        gentest::expect(false, "template checks");
    } else {
        gentest::expect(s == "Hello" || s == "World", "string axis");
    }
}
