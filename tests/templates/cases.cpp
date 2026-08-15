#include "cases.hpp"

void local_struct_pack(LocalPoint p, LocalPoint q) {
    const bool row1 = (p.x == 1 && p.y == 2 && q.x == 3 && q.y == 4);
    const bool row2 = (p.x == 5 && p.y == 6 && q.x == 7 && q.y == 8);
    gentest::expect(row1 || row2, "LocalPoint pack rows");
}

namespace templates {

void params_test(int i) { gentest::expect(i == 0 || i == 10 || i == 100, "value in set {0,10,100}"); }

} // namespace templates

namespace templates {

void pairs(int a, int b) { gentest::expect((a == 1 || a == 2) && (b == 5 || b == 6), "cartesian pairs valid"); }

} // namespace templates

namespace templates {

// NOLINTNEXTLINE(performance-unnecessary-value-param)
void strs(std::string s) { gentest::expect(s == "a" || s == "b", "strings axis values"); }

} // namespace templates

namespace templates {

// NOLINTNEXTLINE(performance-unnecessary-value-param)
void pack(int a, std::string b) {
    const bool row1 = (a == 42 && b == "a");
    const bool row2 = (a == 7 && b == "b");
    gentest::expect(row1 || row2, "parameters_pack rows valid");
}

} // namespace templates

namespace templates {

void raw_msec(std::chrono::milliseconds v) { gentest::expect_eq(v.count(), 10LL, "raw milliseconds value"); }

} // namespace templates

namespace templates {

void chars(char c) { gentest::expect(c == 'a' || c == 'z', "char axis values"); }

} // namespace templates

namespace templates {

// NOLINTNEXTLINE(performance-unnecessary-value-param)
void wstrs(std::wstring s) { gentest::expect(s == L"Alpha", "wide string literal value"); }

} // namespace templates

namespace templates {

void bool_params(bool b) { gentest::expect(b == true || b == false, "bool axis values"); }

} // namespace templates

namespace templates {

void sv_params(std::string_view sv) { gentest::expect(sv == "hello" || sv == "world", "string_view values"); }

} // namespace templates

namespace templates {

void cstr_params(const char *s) {
    std::string str{s};
    gentest::expect(str == "qux" || str == "baz", "cstr values");
}

} // namespace templates

namespace templates {

// NOLINTNEXTLINE(performance-unnecessary-value-param)
void u8strs(std::u8string s) { gentest::expect(s == u8"alpha" || s == u8"beta", "u8string values"); }

} // namespace templates

namespace templates {

void wcstr_params(const wchar_t *s) {
    std::wstring ws{s};
    gentest::expect(ws == L"Wide" || ws == L"X", "wchar_t* values");
}

} // namespace templates

namespace templates {

void u16cstr_params(const char16_t *s) {
    std::u16string us{s};
    gentest::expect(us == u"hello" || us == u"w", "char16_t* values");
}

} // namespace templates

namespace templates {

void u32cstr_params(const char32_t *s) {
    std::u32string us{s};
    gentest::expect(us == U"Cat" || us == U"Dog", "char32_t* values");
}

} // namespace templates

namespace templates {

void wsv_params(std::wstring_view sv) { gentest::expect(sv == L"Alpha" || sv == L"Beta", "wstring_view values"); }

} // namespace templates

namespace templates {

// NOLINTNEXTLINE(performance-unnecessary-value-param)
void u16strs(std::u16string s) { gentest::expect(s == u"alpha" || s == u"beta", "u16string values"); }

} // namespace templates

namespace templates {

void u32sv_params(std::u32string_view sv) { gentest::expect(sv == U"One" || sv == U"Two", "u32string_view values"); }

} // namespace templates

namespace templates {

// NOLINTNEXTLINE(performance-unnecessary-value-param)
void bool_and_str(bool b, std::string s) {
    gentest::expect((b == true || b == false) && (s == "Hello" || s == "World"), "bool+string values");
}

} // namespace templates

namespace templates {

void pack_cstr_bool(const char *s, bool b) {
    std::string str{s};
    const bool  row1 = (str == "Alpha" && b == true);
    const bool  row2 = (str == "Beta" && b == false);
    gentest::expect(row1 || row2, "pack cstr+bool rows");
}

} // namespace templates

void local_struct_axis(LocalPoint p) { gentest::expect((p.x == 1 && p.y == 2) || (p.x == 3 && p.y == 4), "LocalPoint matches"); }

void multi_params_split(int a, int b) { gentest::expect((a == 1 || a == 2) && b == 10, "split params across blocks"); }

void multi_pack_split(int a, int b, int c) {
    const bool row1 = (a == 1 && b == 2 && c == 5);
    const bool row2 = (a == 3 && b == 4 && c == 5);
    gentest::expect(row1 || row2, "split packs across blocks");
}
