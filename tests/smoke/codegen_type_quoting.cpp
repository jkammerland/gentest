#include "gentest/attributes.h"

#include <string>
#include <string_view>

[[using gentest: test("smoke/type_quoting/cstr"), parameters(s, token, "literal")]]
void cstr_params(const char *s) {
    (void)s;
}

[[using gentest: test("smoke/type_quoting/wsv"), parameters(s, Wide, L"X")]]
void wide_string_view(std::wstring_view s) {
    (void)s;
}

[[using gentest: test("smoke/type_quoting/u8sv"), parameters(s, alpha, u8"beta")]]
void utf8_string_view(std::u8string_view s) {
    (void)s;
}

[[using gentest: test("smoke/type_quoting/u16ptr"), parameters(s, hello, u"w")]]
void utf16_cstr(const char16_t *s) {
    (void)s;
}

[[using gentest: test("smoke/type_quoting/u32ptr"), parameters(s, Cat, U"Dog")]]
void utf32_cstr(const char32_t *s) {
    (void)s;
}

[[using gentest: test("smoke/type_quoting/char"), parameters(c, a, '\\', '\'')]]
void char_params(char c) {
    (void)c;
}

[[using gentest: test("smoke/type_quoting/wchar"), parameters(c, Z, L'Q')]]
void wchar_params(wchar_t c) {
    (void)c;
}

[[using gentest: test("smoke/type_quoting/char8"), parameters(c, B, u8'C')]]
void char8_params(char8_t c) {
    (void)c;
}

[[using gentest: test("smoke/type_quoting/char16"), parameters(c, C, u'D')]]
void char16_params(char16_t c) {
    (void)c;
}

[[using gentest: test("smoke/type_quoting/char32"), parameters(c, E, U'F')]]
void char32_params(char32_t c) {
    (void)c;
}
