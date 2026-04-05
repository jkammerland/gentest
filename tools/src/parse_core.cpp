// Implementation of core attribute list parsing API with no Clang deps

#include "parse_core.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
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

char previous_non_space(std::string_view text) {
    for (std::size_t idx = text.size(); idx > 0; --idx) {
        const char ch = text[idx - 1];
        if (std::isspace(static_cast<unsigned char>(ch)) == 0) {
            return ch;
        }
    }
    return '\0';
}

char next_non_space(std::string_view text, std::size_t index) {
    while (index < text.size()) {
        const char ch = text[index];
        if (std::isspace(static_cast<unsigned char>(ch)) == 0) {
            return ch;
        }
        ++index;
    }
    return '\0';
}

bool is_likely_template_left(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == ':' || ch == '>' || ch == ')' || ch == ']';
}

bool is_likely_template_right(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == ':' || ch == '(' || ch == '+' || ch == '-' || ch == '\'';
}

bool is_word_char(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_'; }

bool is_char_literal_prefix(std::string_view text, std::size_t index) {
    if (index >= 1 && (text[index - 1] == 'L' || text[index - 1] == 'u' || text[index - 1] == 'U')) {
        return index == 1 || !is_word_char(text[index - 2]);
    }
    if (index >= 2 && text[index - 2] == 'u' && text[index - 1] == '8') {
        return index == 2 || !is_word_char(text[index - 3]);
    }
    return false;
}

bool should_enter_char_literal(std::string_view text, std::size_t index) {
    if (index >= text.size() || text[index] != '\'') {
        return false;
    }
    if (is_char_literal_prefix(text, index)) {
        return true;
    }

    const char prev = index > 0 ? text[index - 1] : '\0';
    const char next = index + 1 < text.size() ? text[index + 1] : '\0';
    if (next == '\0') {
        return false;
    }
    if (std::isalnum(static_cast<unsigned char>(prev)) != 0 && std::isalnum(static_cast<unsigned char>(next)) != 0) {
        return false;
    }
    return true;
}

struct RawStringStart {
    std::size_t prefix_length = 0;
    std::string delimiter;
};

std::optional<RawStringStart> detect_raw_string_start(std::string_view text, std::size_t index) {
    std::size_t cursor = index;
    if (text.substr(index).starts_with("R\"")) {
        cursor += 2;
    } else if (text.substr(index).starts_with("uR\"") || text.substr(index).starts_with("UR\"") || text.substr(index).starts_with("LR\"")) {
        cursor += 3;
    } else if (text.substr(index).starts_with("u8R\"")) {
        cursor += 4;
    } else {
        return std::nullopt;
    }

    const std::size_t delim_start = cursor;
    while (cursor < text.size() && text[cursor] != '(') {
        const char ch = text[cursor];
        if (ch == '\\' || ch == ')' || std::isspace(static_cast<unsigned char>(ch)) != 0) {
            return std::nullopt;
        }
        ++cursor;
    }
    if (cursor >= text.size() || text[cursor] != '(') {
        return std::nullopt;
    }
    return RawStringStart{
        .prefix_length = cursor - index + 1,
        .delimiter     = std::string(text.substr(delim_start, cursor - delim_start)),
    };
}

bool raw_string_closes_here(std::string_view text, std::size_t index, std::string_view delimiter) {
    if (index >= text.size() || text[index] != ')') {
        return false;
    }
    const std::size_t delimiter_start = index + 1;
    if (delimiter_start + delimiter.size() >= text.size()) {
        return false;
    }
    return text.substr(delimiter_start, delimiter.size()) == delimiter && text[delimiter_start + delimiter.size()] == '"';
}

std::string next_identifier_token(std::string_view text, std::size_t index) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
    }
    if (index >= text.size()) {
        return {};
    }
    const char first = text[index];
    if (std::isalpha(static_cast<unsigned char>(first)) == 0 && first != '_') {
        return {};
    }

    std::size_t end = index + 1;
    while (end < text.size()) {
        const char ch = text[end];
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
            ++end;
            continue;
        }
        break;
    }
    return std::string(text.substr(index, end - index));
}

bool has_matching_angle_close(std::string_view text, std::size_t open_index) {
    int  depth        = 0;
    int  nested_angle = 0;
    bool in_string    = false;
    bool in_char      = false;
    bool escape_next  = false;

    for (std::size_t idx = open_index + 1; idx < text.size(); ++idx) {
        const char ch = text[idx];
        if (in_string) {
            if (escape_next) {
                escape_next = false;
            } else if (ch == '\\') {
                escape_next = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (in_char) {
            if (escape_next) {
                escape_next = false;
            } else if (ch == '\\') {
                escape_next = true;
            } else if (ch == '\'') {
                in_char = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '\'' && should_enter_char_literal(text, idx)) {
            in_char = true;
            continue;
        }

        if (ch == '(' || ch == '[' || ch == '{') {
            ++depth;
            continue;
        }
        if (ch == ')' || ch == ']' || ch == '}') {
            if (depth > 0) {
                --depth;
            }
            continue;
        }
        if (depth > 0) {
            continue;
        }

        if (ch == '<') {
            ++nested_angle;
            continue;
        }
        if (ch == '>') {
            if (nested_angle == 0) {
                const char follower = next_non_space(text, idx + 1);
                if (follower == '\0') {
                    return true;
                }
                switch (follower) {
                case '_':
                case ',':
                case ')':
                case ']':
                case '}':
                case '{':
                case '(':
                case ':':
                case ';':
                case '*':
                case '&': return true;
                default: {
                    const std::string token = next_identifier_token(text, idx + 1);
                    if (token == "const" || token == "volatile") {
                        return true;
                    }
                    if (token.empty()) {
                        return false;
                    }
                    std::size_t token_start = idx + 1;
                    while (token_start < text.size() && std::isspace(static_cast<unsigned char>(text[token_start])) != 0) {
                        ++token_start;
                    }
                    const std::size_t token_end = token_start + token.size();
                    const char        after     = next_non_space(text, token_end);
                    return after == '{';
                }
                }
            }
            --nested_angle;
        }
    }
    return false;
}

std::vector<std::string> split_arguments(std::string_view arguments) {
    std::vector<std::string> parts;
    std::string              current;
    int                      depth       = 0;
    int                      angle_depth = 0;
    bool                     in_string   = false;
    bool                     in_char     = false;
    bool                     escape_next = false;

    auto should_open_angle = [&](std::size_t idx) {
        const char prev = previous_non_space(current);
        if (prev == '\0' || !is_likely_template_left(prev)) {
            return false;
        }
        const char next = next_non_space(arguments, idx + 1);
        if (next == '\0' || next == '<' || next == '=' || !is_likely_template_right(next)) {
            return false;
        }
        return has_matching_angle_close(arguments, idx);
    };

    for (std::size_t idx = 0; idx < arguments.size(); ++idx) {
        const char ch = arguments[idx];
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
        if (in_char) {
            current.push_back(ch);
            if (escape_next) {
                escape_next = false;
            } else if (ch == '\\') {
                escape_next = true;
            } else if (ch == '\'') {
                in_char = false;
            }
            continue;
        }

        switch (ch) {
        case '"':
            in_string = true;
            current.push_back(ch);
            break;
        case '\'':
            if (should_enter_char_literal(arguments, idx)) {
                in_char = true;
            }
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
        case '<':
            if (should_open_angle(idx)) {
                ++angle_depth;
            }
            current.push_back(ch);
            break;
        case '>':
            if (angle_depth > 0) {
                --angle_depth;
            }
            current.push_back(ch);
            break;
        case ',':
            if (depth == 0 && angle_depth == 0) {
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

        // Support scoped attribute tokens inside a list, e.g.
        // [[gentest::test("x"), gentest::fast]].
        // For gentest-qualified tokens we keep only the attribute name ("test",
        // "fast"), while other namespaces are preserved as "ns::attr" so
        // validation can reject them explicitly.
        while (true) {
            std::size_t scope_cursor = index;
            while (scope_cursor < list.size() && std::isspace(static_cast<unsigned char>(list[scope_cursor])) != 0) {
                ++scope_cursor;
            }
            if (scope_cursor + 1 >= list.size() || list[scope_cursor] != ':' || list[scope_cursor + 1] != ':') {
                break;
            }

            scope_cursor += 2;
            while (scope_cursor < list.size() && std::isspace(static_cast<unsigned char>(list[scope_cursor])) != 0) {
                ++scope_cursor;
            }
            if (scope_cursor >= list.size()) {
                break;
            }
            if (!std::isalpha(static_cast<unsigned char>(list[scope_cursor])) && list[scope_cursor] != '_') {
                break;
            }

            const std::size_t scoped_name_start = scope_cursor;
            ++scope_cursor;
            while (scope_cursor < list.size() && is_identifier_char(list[scope_cursor])) {
                ++scope_cursor;
            }

            const std::string scoped_name = std::string(list.substr(scoped_name_start, scope_cursor - scoped_name_start));
            if (name == "gentest") {
                name = scoped_name;
            } else {
                name += "::";
                name += scoped_name;
            }
            index = scope_cursor;
        }

        skip_whitespace(index);

        std::vector<std::string> args;
        if (index < list.size() && list[index] == '(') {
            ++index;
            const std::size_t args_start = index;
            int               depth      = 1;
            bool              in_string  = false;
            bool              in_char    = false;
            bool              in_raw     = false;
            bool              escape     = false;
            std::string       raw_delimiter;
            for (; index < list.size(); ++index) {
                const char ch = list[index];
                if (in_raw) {
                    if (raw_string_closes_here(list, index, raw_delimiter)) {
                        in_raw = false;
                        index += raw_delimiter.size() + 1;
                    }
                    continue;
                }
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
                if (in_char) {
                    if (escape) {
                        escape = false;
                    } else if (ch == '\\') {
                        escape = true;
                    } else if (ch == '\'') {
                        in_char = false;
                    }
                    continue;
                }
                if (const auto raw_start = detect_raw_string_start(list, index); raw_start.has_value()) {
                    in_raw        = true;
                    raw_delimiter = raw_start->delimiter;
                    index += raw_start->prefix_length - 1;
                    continue;
                }
                if (ch == '"') {
                    in_string = true;
                    continue;
                }
                if (ch == '\'' && should_enter_char_literal(list, index)) {
                    in_char = true;
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
