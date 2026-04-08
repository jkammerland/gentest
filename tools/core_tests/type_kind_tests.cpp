#include "type_kind.hpp"

#include <iostream>
#include <string>
#include <string_view>

using gentest::codegen::classify_type;
using gentest::codegen::quote_for_type;
using gentest::codegen::TypeKind;

namespace {

struct Run {
    int  failures = 0;
    void expect(bool ok, std::string_view msg) {
        if (!ok) {
            ++failures;
            std::cerr << "FAIL: " << msg << "\n";
        }
    }
};

} // namespace

int main() {
    Run t;

    t.expect(classify_type(" raw ") == TypeKind::Raw, "raw type classification");
    t.expect(classify_type("const std::string&") == TypeKind::String, "std::string classification");
    t.expect(classify_type("volatile std::string_view&&") == TypeKind::String, "std::string_view classification");
    t.expect(classify_type("const char *") == TypeKind::String, "char pointer classification");
    t.expect(classify_type("const wchar_t*") == TypeKind::String, "wchar_t pointer classification");
    t.expect(classify_type("const char8_t *") == TypeKind::String, "char8_t pointer classification");
    t.expect(classify_type("const char16_t*") == TypeKind::String, "char16_t pointer classification");
    t.expect(classify_type("const char32_t *") == TypeKind::String, "char32_t pointer classification");
    t.expect(classify_type("char") == TypeKind::Char, "char classification");
    t.expect(classify_type("wchar_t") == TypeKind::Char, "wchar_t classification");
    t.expect(classify_type("char8_t") == TypeKind::Char, "char8_t classification");
    t.expect(classify_type("char16_t") == TypeKind::Char, "char16_t classification");
    t.expect(classify_type("char32_t") == TypeKind::Char, "char32_t classification");
    t.expect(classify_type("int") == TypeKind::Other, "other classification");

    t.expect(quote_for_type(TypeKind::String, "  \"already\"  ", "std::string") == "  \"already\"  ", "plain string literal is preserved");
    t.expect(quote_for_type(TypeKind::String, " L\"wide\" ", "std::wstring") == " L\"wide\" ", "wide string literal is preserved");
    t.expect(quote_for_type(TypeKind::String, " u8\"utf8\" ", "std::u8string_view") == " u8\"utf8\" ", "u8 string literal is preserved");
    t.expect(quote_for_type(TypeKind::String, " u\"utf16\" ", "std::u16string") == " u\"utf16\" ", "u16 string literal is preserved");
    t.expect(quote_for_type(TypeKind::String, " U\"utf32\" ", "std::u32string_view") == " U\"utf32\" ", "u32 string literal is preserved");
    t.expect(quote_for_type(TypeKind::String, "tab\tline\nquote\"", "std::string") == "\"tab\\tline\\nquote\\\"\"",
             "plain string quoting escapes content");
    t.expect(quote_for_type(TypeKind::String, "wide", "std::wstring_view") == "L\"wide\"", "wstring prefix selection");
    t.expect(quote_for_type(TypeKind::String, "utf8", "std::u8string") == "u8\"utf8\"", "u8string prefix selection");
    t.expect(quote_for_type(TypeKind::String, "utf16", "const char16_t*") == "u\"utf16\"", "char16_t pointer prefix selection");
    t.expect(quote_for_type(TypeKind::String, "utf32", "const char32_t *") == "U\"utf32\"", "char32_t pointer prefix selection");

    t.expect(quote_for_type(TypeKind::Char, " 'x' ", "char") == " 'x' ", "char literal is preserved");
    t.expect(quote_for_type(TypeKind::Char, " L'x' ", "wchar_t") == " L'x' ", "wide char literal is preserved");
    t.expect(quote_for_type(TypeKind::Char, " u'x' ", "char16_t") == " u'x' ", "u16 char literal is preserved");
    t.expect(quote_for_type(TypeKind::Char, " U'x' ", "char32_t") == " U'x' ", "u32 char literal is preserved");
    t.expect(quote_for_type(TypeKind::Char, " u8'x' ", "char8_t") == " u8'x' ", "u8 char literal is preserved");
    t.expect(quote_for_type(TypeKind::Char, "\\", "char") == "'\\\\'", "single char quoting escapes backslash");
    t.expect(quote_for_type(TypeKind::Char, "ab", "char") == "ab", "multi-char token stays unquoted");

    t.expect(quote_for_type(TypeKind::Other, "value", "int") == "value", "other token passes through");
    t.expect(quote_for_type(TypeKind::Raw, "payload", "raw") == "payload", "raw token passes through");

    if (t.failures != 0) {
        std::cerr << "Total failures: " << t.failures << "\n";
        return 1;
    }
    return 0;
}
