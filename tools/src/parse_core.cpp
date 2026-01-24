// Implementation of core attribute list parsing API with no Clang deps

#include "parse_core.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::codegen {
namespace {

bool is_identifier_char(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '-'; }

std::string trim_copy(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

std::string unquote(std::string_view value) {
    std::string trimmed = trim_copy(value);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        std::string decoded;
        decoded.reserve(trimmed.size() - 2);
        bool escape = false;
        for (std::size_t idx = 1; idx + 1 < trimmed.size(); ++idx) {
            const char ch = trimmed[idx];
            if (escape) {
                switch (ch) {
                case '\\': decoded.push_back('\\'); break;
                case '"': decoded.push_back('"'); break;
                case 'n': decoded.push_back('\n'); break;
                case 'r': decoded.push_back('\r'); break;
                case 't': decoded.push_back('\t'); break;
                default: decoded.push_back(ch); break;
                }
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else {
                decoded.push_back(ch);
            }
        }
        if (escape) {
            decoded.push_back('\\');
        }
        return decoded;
    }
    return trimmed;
}

std::vector<std::string> split_arguments(std::string_view arguments) {
    std::vector<std::string> parts;
    std::string              current;
    int                      depth       = 0;
    bool                     in_string   = false;
    bool                     escape_next = false;

    for (char ch : arguments) {
        if (in_string) {
            current.push_back(ch);
            if (escape_next) {
                escape_next = false;
            } else if (ch == '\\') {
                escape_next = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        switch (ch) {
        case '"':
            in_string = true;
            current.push_back(ch);
            break;
        case '(':
        case '[':
        case '{':
            ++depth;
            current.push_back(ch);
            break;
        case ')':
        case ']':
        case '}':
            if (depth > 0) {
                --depth;
            }
            current.push_back(ch);
            break;
        case ',':
            if (depth == 0) {
                auto token = trim_copy(current);
                if (!token.empty()) {
                    parts.push_back(unquote(token));
                }
                current.clear();
                break;
            }
            [[fallthrough]];
        default: current.push_back(ch); break;
        }
    }

    auto token = trim_copy(current);
    if (!token.empty()) {
        parts.push_back(unquote(token));
    }
    return parts;
}

} // namespace

auto parse_attribute_list(std::string_view list) -> std::vector<ParsedAttribute> {
    std::vector<ParsedAttribute> attributes;
    std::size_t                  index = 0;

    auto skip_whitespace = [&](std::size_t &cursor) {
        while (cursor < list.size() && std::isspace(static_cast<unsigned char>(list[cursor])) != 0) {
            ++cursor;
        }
    };

    while (index < list.size()) {
        skip_whitespace(index);
        if (index >= list.size()) {
            break;
        }
        if (list[index] == ',') {
            ++index;
            continue;
        }

        const std::size_t name_start = index;
        if (!std::isalpha(static_cast<unsigned char>(list[index])) && list[index] != '_') {
            ++index;
            continue;
        }
        ++index;
        while (index < list.size() && is_identifier_char(list[index])) {
            ++index;
        }
        std::string name = std::string(list.substr(name_start, index - name_start));

        skip_whitespace(index);

        std::vector<std::string> args;
        if (index < list.size() && list[index] == '(') {
            ++index;
            const std::size_t args_start = index;
            int               depth      = 1;
            bool              in_string  = false;
            bool              escape     = false;
            for (; index < list.size(); ++index) {
                const char ch = list[index];
                if (in_string) {
                    if (escape) {
                        escape = false;
                    } else if (ch == '\\') {
                        escape = true;
                    } else if (ch == '"') {
                        in_string = false;
                    }
                    continue;
                }
                if (ch == '"') {
                    in_string = true;
                    continue;
                }
                if (ch == '(') {
                    ++depth;
                    continue;
                }
                if (ch == ')') {
                    --depth;
                    if (depth == 0) {
                        auto inside = list.substr(args_start, index - args_start);
                        args        = split_arguments(inside);
                        ++index; // consume ')'
                        break;
                    }
                }
            }
        }

        attributes.push_back(ParsedAttribute{.name = std::move(name), .arguments = std::move(args)});

        while (index < list.size() && list[index] != ',') {
            if (!std::isspace(static_cast<unsigned char>(list[index]))) {
                break;
            }
            ++index;
        }
        if (index < list.size() && list[index] == ',') {
            ++index;
        }
    }

    return attributes;
}

} // namespace gentest::codegen
