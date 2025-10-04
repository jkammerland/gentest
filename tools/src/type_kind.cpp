// Implementation of type classification and quoting helpers

#include "type_kind.hpp"
#include "render.hpp" // for escape_string

#include <algorithm>

namespace gentest::codegen {

static std::string normalize(std::string_view sv) {
    std::string s(sv);
    // strip qualifiers and spaces
    const char* quals[] = {"const ", "volatile ", "&", "&&", "*"};
    for (const auto* q : quals) {
        std::string::size_type pos;
        const auto qlen = std::char_traits<char>::length(q);
        while ((pos = s.find(q)) != std::string::npos) s.erase(pos, qlen);
    }
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c){ return std::isspace(c); }), s.end());
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
}

static std::string normalize_keep_ptr(std::string_view sv) {
    std::string s(sv);
    // strip qualifiers and references, keep '*'
    const char* quals[] = {"const ", "volatile ", "&", "&&"};
    for (const auto* q : quals) {
        std::string::size_type pos;
        const auto qlen = std::char_traits<char>::length(q);
        while ((pos = s.find(q)) != std::string::npos) s.erase(pos, qlen);
    }
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c){ return std::isspace(c); }), s.end());
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
}

TypeKind classify_type(std::string_view type_name) {
    auto t = normalize(type_name);
    auto p = normalize_keep_ptr(type_name);
    if (t == "raw") return TypeKind::Raw;
    // String-like must be checked before plain Char so that pointers classify as strings
    if (t.find("string_view") != std::string::npos || t.find("string") != std::string::npos || p.find("char*") != std::string::npos ||
        p.find("wchar_t*") != std::string::npos || p.find("char8_t*") != std::string::npos || p.find("char16_t*") != std::string::npos ||
        p.find("char32_t*") != std::string::npos) return TypeKind::String;
    if (t == "char" || t == "wchar_t" || t == "char8_t" || t == "char16_t" || t == "char32_t") return TypeKind::Char;
    // Could detect integer/floating by exact names; leave as Other for now
    return TypeKind::Other;
}

std::string quote_for_type(TypeKind kind, std::string_view token, std::string_view type_name) {
    auto is_str_lit = [](std::string s) {
        auto trim = [](std::string &x){ auto l=x.find_first_not_of(" \t\n\r"); auto r=x.find_last_not_of(" \t\n\r"); if(l==std::string::npos){x.clear();} else { x = x.substr(l, r-l+1);} };
        trim(s);
        if (s.size()>=2 && s.front()=='"' && s.back()=='"') return true;
        if (s.size()>=3 && (s[0]=='L' || s[0]=='u' || s[0]=='U') && s[1]=='"' && s.back()=='"') return true;
        if (s.size()>=4 && s[0]=='u' && s[1]=='8' && s[2]=='"' && s.back()=='"') return true;
        return false;
    };
    auto is_char_lit = [](std::string s) {
        auto trim = [](std::string &x){ auto l=x.find_first_not_of(" \t\n\r"); auto r=x.find_last_not_of(" \t\n\r"); if(l==std::string::npos){x.clear();} else { x = x.substr(l, r-l+1);} };
        trim(s);
        if (s.size()>=3 && s.front()=='\'' && s.back()=='\'') return true;
        if (s.size()>=4 && (s[0]=='L' || s[0]=='u' || s[0]=='U') && s[1]=='\'' && s.back()=='\'') return true;
        if (s.size()>=5 && s[0]=='u' && s[1]=='8' && s[2]=='\'' && s.back()=='\'') return true;
        return false;
    };
    switch (kind) {
    case TypeKind::String: {
        std::string t(token);
        if (is_str_lit(t)) return t;
        auto n = normalize(type_name);
        auto np = normalize_keep_ptr(type_name);
        const char* prefix = "";
        if (n.find("wstring") != std::string::npos || n.find("wstring_view") != std::string::npos || np.find("wchar_t*") != std::string::npos) prefix = "L";
        else if (n.find("u8string") != std::string::npos || n.find("u8string_view") != std::string::npos || np.find("char8_t*") != std::string::npos) prefix = "u8";
        else if (n.find("u16string") != std::string::npos || n.find("u16string_view") != std::string::npos || np.find("char16_t*") != std::string::npos) prefix = "u";
        else if (n.find("u32string") != std::string::npos || n.find("u32string_view") != std::string::npos || np.find("char32_t*") != std::string::npos) prefix = "U";
        std::string out;
        out += prefix;
        out += '"'; out += render::escape_string(t); out += '"';
        return out;
    }
    case TypeKind::Char: {
        std::string t(token);
        if (is_char_lit(t)) return t;
        if (t.size() == 1) { std::string out; out += '\''; out += render::escape_string(t); out += '\''; return out; }
        return t;
    }
    default: return std::string(token);
    }
}

} // namespace gentest::codegen
