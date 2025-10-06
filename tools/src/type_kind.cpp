// Implementation of type classification and quoting helpers

#include "type_kind.hpp"

#include "render.hpp" // for escape_string

#include <algorithm>
#include <array>
#include <cctype>

namespace gentest::codegen {

namespace {

enum class PointerPolicy { Strip, Keep };

constexpr char kSingleQuote = '\'';

std::string normalize_impl(std::string_view sv, PointerPolicy policy) {
    std::string s(sv);
    auto        erase_all = [&](std::string_view needle) {
        std::string::size_type pos;
        while ((pos = s.find(needle)) != std::string::npos) {
            s.erase(pos, needle.size());
        }
    };

    for (std::string_view qualifier :
         std::array{std::string_view("const "), std::string_view("volatile "), std::string_view("&"), std::string_view("&&")}) {
        erase_all(qualifier);
    }
    if (policy == PointerPolicy::Strip) {
        erase_all("*");
    }

    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c) != 0; }), s.end());
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string normalize(std::string_view sv) { return normalize_impl(sv, PointerPolicy::Strip); }
std::string normalize_keep_ptr(std::string_view sv) { return normalize_impl(sv, PointerPolicy::Keep); }

std::string_view trim_view(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

bool is_string_literal(std::string_view token) {
    token = trim_view(token);
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
        return true;
    if (token.size() >= 3 && (token[0] == 'L' || token[0] == 'u' || token[0] == 'U') && token[1] == '"' && token.back() == '"')
        return true;
    if (token.size() >= 4 && token[0] == 'u' && token[1] == '8' && token[2] == '"' && token.back() == '"')
        return true;
    return false;
}

bool is_char_literal(std::string_view token) {
    token = trim_view(token);
    if (token.size() >= 3 && token.front() == '\'' && token.back() == '\'')
        return true;
    if (token.size() >= 4 && (token[0] == 'L' || token[0] == 'u' || token[0] == 'U') && token[1] == '\'' && token.back() == '\'')
        return true;
    if (token.size() >= 5 && token[0] == 'u' && token[1] == '8' && token[2] == '\'' && token.back() == '\'')
        return true;
    return false;
}

} // namespace

TypeKind classify_type(std::string_view type_name) {
    auto t = normalize(type_name);
    auto p = normalize_keep_ptr(type_name);
    if (t == "raw")
        return TypeKind::Raw;
    // String-like must be checked before plain Char so that pointers classify as strings
    if (t.find("string_view") != std::string::npos || t.find("string") != std::string::npos || p.find("char*") != std::string::npos ||
        p.find("wchar_t*") != std::string::npos || p.find("char8_t*") != std::string::npos || p.find("char16_t*") != std::string::npos ||
        p.find("char32_t*") != std::string::npos)
        return TypeKind::String;
    if (t == "char" || t == "wchar_t" || t == "char8_t" || t == "char16_t" || t == "char32_t")
        return TypeKind::Char;
    // Could detect integer/floating by exact names; leave as Other for now
    return TypeKind::Other;
}

std::string quote_for_type(TypeKind kind, std::string_view token, std::string_view type_name) {
    switch (kind) {
    case TypeKind::String: {
        if (is_string_literal(token))
            return std::string(token);
        auto        n      = normalize(type_name);
        auto        np     = normalize_keep_ptr(type_name);
        const char *prefix = "";
        if (n.find("wstring") != std::string::npos || n.find("wstring_view") != std::string::npos ||
            np.find("wchar_t*") != std::string::npos)
            prefix = "L";
        else if (n.find("u8string") != std::string::npos || n.find("u8string_view") != std::string::npos ||
                 np.find("char8_t*") != std::string::npos)
            prefix = "u8";
        else if (n.find("u16string") != std::string::npos || n.find("u16string_view") != std::string::npos ||
                 np.find("char16_t*") != std::string::npos)
            prefix = "u";
        else if (n.find("u32string") != std::string::npos || n.find("u32string_view") != std::string::npos ||
                 np.find("char32_t*") != std::string::npos)
            prefix = "U";
        std::string out;
        out += prefix;
        out.push_back('"');
        out += render::escape_string(token);
        out.push_back('"');
        return out;
    }
    case TypeKind::Char: {
        if (is_char_literal(token))
            return std::string(token);
        if (token.size() == 1) {
            std::string out;
            out.push_back(kSingleQuote);
            out += render::escape_string(token);
            out.push_back(kSingleQuote);
            return out;
        }
        return std::string(token);
    }
    default: return std::string(token);
    }
}

} // namespace gentest::codegen
