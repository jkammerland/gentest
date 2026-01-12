#include "gentest/runner.h"
#include "templates/template_cases.hpp"

#include <chrono>
#include <string>
#include <string_view>

namespace templates {

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

// Boolean parameter axis

[[using gentest: test("bool_params"), parameters(b, true, false)]]
void bool_params(bool b) {
    gentest::expect(b == true || b == false, "bool axis values");
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

// Local struct defined in template_cases.hpp, used as a parameter via named parameters

[[using gentest: test("local_struct/axis"), parameters(p, LocalPoint{1, 2}, LocalPoint{3, 4})]]
void local_struct_axis(LocalPoint p) {
    gentest::expect((p.x == 1 && p.y == 2) || (p.x == 3 && p.y == 4), "LocalPoint matches");
}

[[using gentest: test("local_struct/pack"),
  parameters_pack((p, q), (LocalPoint{1, 2}, LocalPoint{3, 4}), (LocalPoint{5, 6}, LocalPoint{7, 8}))]]
void local_struct_pack(LocalPoint p, LocalPoint q) {
    const bool row1 = (p.x == 1 && p.y == 2 && q.x == 3 && q.y == 4);
    const bool row2 = (p.x == 5 && p.y == 6 && q.x == 7 && q.y == 8);
    gentest::expect(row1 || row2, "LocalPoint pack rows");
}

// Multiple [[...]] blocks: parameters split across blocks

[[using gentest: test("multi_blocks/params_split")]] [[using gentest: parameters(a, 1, 2)]] [[using gentest: parameters(b, 10)]]
void multi_params_split(int a, int b) {
    gentest::expect((a == 1 || a == 2) && b == 10, "split params across blocks");
}

// Multiple [[...]] blocks: two packs split across blocks

[[using gentest: test("multi_blocks/pack_split")]] [[using gentest: parameters_pack((a, b), (1, 2),
                                                                                    (3, 4))]] [[using gentest: parameters_pack((c), (5))]]
void multi_pack_split(int a, int b, int c) {
    const bool row1 = (a == 1 && b == 2 && c == 5);
    const bool row2 = (a == 3 && b == 4 && c == 5);
    gentest::expect(row1 || row2, "split packs across blocks");
}
