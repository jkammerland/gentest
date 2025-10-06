#include "gentest/runner.h"

#include <chrono>
#include <cstddef>
#include <string_view>
#include <type_traits>

namespace [[using gentest: suite("templates")]] templates {

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

// Parameterized test

[[using gentest: test("params"), parameters(i, 0, 10, 100)]]
void params_test(int i) {
    gentest::expect(i == 0 || i == 10 || i == 100, "value in set {0,10,100}");
}

// Multi-argument parameterization across separate blocks

[[using gentest: test("pairs")]] [[using gentest: parameters(a, 1, 2)]] [[using gentest: parameters(b, 5, 6)]]
void pairs(int a, int b) {
    gentest::expect((a == 1 || a == 2) && (b == 5 || b == 6), "cartesian pairs valid");
}

[[using gentest: test("strs"), parameters(s, "a", b)]]
void strs(std::string s) {
    gentest::expect(s == "a" || s == "b", "strings axis values");
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

// parameters_pack: bundle multiple args per row

[[using gentest: test("pack"), parameters_pack((a, b), (42, a), (7, "b"))]]
void pack(int a, std::string b) {
    const bool row1 = (a == 42 && b == "a");
    const bool row2 = (a == 7 && b == "b");
    gentest::expect(row1 || row2, "parameters_pack rows valid");
}

// Raw axis: verbatim expressions

[[using gentest: test("raw"), parameters(v, std::chrono::milliseconds{10})]]
void raw_msec(std::chrono::milliseconds v) {
    gentest::expect_eq(v.count(), 10LL, "raw milliseconds value");
}

// Char-like literals

[[using gentest: test("chars"), parameters(c, a, 'z')]]
void chars(char c) {
    gentest::expect(c == 'a' || c == 'z', "char axis values");
}

// Wide/UTF strings

[[using gentest: test("wstrs"), parameters(s, Alpha)]]
void wstrs(std::wstring s) {
    gentest::expect(s == L"Alpha", "wide string literal value");
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

// Boolean parameter axis
[[using gentest: test("bool_params"), parameters(b, true, false)]]
void bool_params(bool b) {
    gentest::expect(b == true || b == false, "bool axis values");
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
namespace ns_outer { namespace ns_inner { enum class Shade { Dark, Light }; } }

template <ns_outer::ns_inner::Shade S>
[[using gentest: test("enum_value_scoped"), template(S, templates::ns_outer::ns_inner::Shade::Dark, templates::ns_outer::ns_inner::Shade::Light)]]
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

// string_view axis with mixed quoted/unquoted
[[using gentest: test("sv_params"), parameters(sv, hello, "world")]]
void sv_params(std::string_view sv) {
    gentest::expect(sv == "hello" || sv == "world", "string_view values");
}

// const char* axis with mixed quoted/unquoted
[[using gentest: test("cstr_params"), parameters(s, qux, "baz")]]
void cstr_params(const char *s) {
    std::string str{s};
    gentest::expect(str == "qux" || str == "baz", "cstr values");
}

// u8string axis ensures correct UTF-8 literal prefixing
[[using gentest: test("u8strs"), parameters(s, alpha, beta)]]
void u8strs(std::u8string s) {
    gentest::expect(s == u8"alpha" || s == u8"beta", "u8string values");
}

// wchar_t* axis with mixed quoted/unquoted
[[using gentest: test("wcstr_params"), parameters(s, Wide, L"X")]]
void wcstr_params(const wchar_t *s) {
    std::wstring ws{s};
    gentest::expect(ws == L"Wide" || ws == L"X", "wchar_t* values");
}

// char16_t* axis
[[using gentest: test("u16cstr_params"), parameters(s, hello, u"w")]]
void u16cstr_params(const char16_t *s) {
    std::u16string us{s};
    gentest::expect(us == u"hello" || us == u"w", "char16_t* values");
}

// char32_t* axis
[[using gentest: test("u32cstr_params"), parameters(s, Cat, U"Dog")]]
void u32cstr_params(const char32_t *s) {
    std::u32string us{s};
    gentest::expect(us == U"Cat" || us == U"Dog", "char32_t* values");
}

// wstring_view axis
[[using gentest: test("wsv_params"), parameters(sv, Alpha, L"Beta")]]
void wsv_params(std::wstring_view sv) {
    gentest::expect(sv == L"Alpha" || sv == L"Beta", "wstring_view values");
}

// u16string axis (non-view)
[[using gentest: test("u16strs"), parameters(s, alpha, u"beta")]]
void u16strs(std::u16string s) {
    gentest::expect(s == u"alpha" || s == u"beta", "u16string values");
}

// u32string_view axis
[[using gentest: test("u32sv_params"), parameters(sv, One, U"Two")]]
void u32sv_params(std::u32string_view sv) {
    gentest::expect(sv == U"One" || sv == U"Two", "u32string_view values");
}

// Combined boolean + string axis
[[using gentest: test("bool_and_str"), parameters(b, true, false), parameters(s, Hello, "World")]]
void bool_and_str(bool b, std::string s) {
    gentest::expect((b == true || b == false) && (s == "Hello" || s == "World"), "bool+string values");
}

// parameters_pack with cstr + bool
[[using gentest: test("pack_cstr_bool"), parameters_pack((s, b), (Alpha, true), ("Beta", false))]]
void pack_cstr_bool(const char *s, bool b) {
    std::string str{s};
    const bool  row1 = (str == "Alpha" && b == true);
    const bool  row2 = (str == "Beta" && b == false);
    gentest::expect(row1 || row2, "pack cstr+bool rows");
}

} // namespace templates
// Enum value template parameter
enum class Color { Red, Green, Blue };

template <Color C>
[[using gentest: test("enum_value"), template(C, Color::Red, Color::Blue)]]
void enum_value() {
    gentest::expect(C == Color::Red || C == Color::Blue, "C in {Red,Blue}");
}

// Local struct defined in the test source (cases.cpp), used as a parameter via named parameters
struct LocalPoint {
    int x;
    int y;
};

[[using gentest: test("local_struct/axis"), parameters(p, LocalPoint{1,2}, LocalPoint{3,4})]]
void local_struct_axis(LocalPoint p) {
    gentest::expect((p.x == 1 && p.y == 2) || (p.x == 3 && p.y == 4), "LocalPoint matches");
}

[[using gentest: test("local_struct/pack"), parameters_pack((p, q), (LocalPoint{1,2}, LocalPoint{3,4}), (LocalPoint{5,6}, LocalPoint{7,8}))]]
void local_struct_pack(LocalPoint p, LocalPoint q) {
    const bool row1 = (p.x == 1 && p.y == 2 && q.x == 3 && q.y == 4);
    const bool row2 = (p.x == 5 && p.y == 6 && q.x == 7 && q.y == 8);
    gentest::expect(row1 || row2, "LocalPoint pack rows");
}

// Multiple [[...]] blocks: parameters split across blocks
[[using gentest: test("multi_blocks/params_split")]]
[[using gentest: parameters(a, 1, 2)]]
[[using gentest: parameters(b, 10)]]
void multi_params_split(int a, int b) {
    gentest::expect((a == 1 || a == 2) && b == 10, "split params across blocks");
}

// Multiple [[...]] blocks: two packs split across blocks
[[using gentest: test("multi_blocks/pack_split")]]
[[using gentest: parameters_pack((a, b), (1, 2), (3, 4))]]
[[using gentest: parameters_pack((c), (5))]]
void multi_pack_split(int a, int b, int c) {
    const bool row1 = (a == 1 && b == 2 && c == 5);
    const bool row2 = (a == 3 && b == 4 && c == 5);
    gentest::expect(row1 || row2, "split packs across blocks");
}

// Multiple [[...]] blocks: mix of templates and parameters split
template <typename T, int N>
[[using gentest: test("multi_blocks/mixed_split")]]
[[using gentest: template(T, int)]]
[[using gentest: template(N, 7)]]
[[using gentest: parameters(s, Hello, "World")]]
void multi_mixed_split(std::string s) {
    if constexpr (!std::is_same_v<T, int> || N != 7) {
        gentest::expect(false, "template checks");
    } else {
        gentest::expect(s == "Hello" || s == "World", "string axis");
    }
}
