// Implementation of core attribute list parsing API with no Clang deps

#include "parse_core.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
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

void append_utf8(std::string &out, std::uint32_t code_point) {
    if (code_point < 0x80U) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800U) {
        out.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
        out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point < 0x10000U) {
        out.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }
}

int hex_digit_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

// Decodes one escape sequence starting at body[index] (the character after
// the backslash). Returns the number of characters consumed, or 0 when the
// sequence is not a valid narrow-string escape; the caller then keeps the
// backslash so nothing is lost.
std::size_t decode_escape(std::string_view body, std::size_t index, std::string &decoded) {
    const char ch = body[index];
    switch (ch) {
    case '\\':
    case '"':
    case '\'':
    case '?': decoded.push_back(ch); return 1;
    case 'a': decoded.push_back('\a'); return 1;
    case 'b': decoded.push_back('\b'); return 1;
    case 'f': decoded.push_back('\f'); return 1;
    case 'n': decoded.push_back('\n'); return 1;
    case 'r': decoded.push_back('\r'); return 1;
    case 't': decoded.push_back('\t'); return 1;
    case 'v': decoded.push_back('\v'); return 1;
    default: break;
    }
    if (ch >= '0' && ch <= '7') {
        unsigned    value  = 0;
        std::size_t length = 0;
        while (length < 3 && index + length < body.size() && body[index + length] >= '0' && body[index + length] <= '7') {
            value = value * 8U + static_cast<unsigned>(body[index + length] - '0');
            ++length;
        }
        if (value > 0xFFU) {
            return 0;
        }
        decoded.push_back(static_cast<char>(value));
        return length;
    }
    if (ch == 'x') {
        unsigned    value  = 0;
        std::size_t length = 1;
        while (index + length < body.size() && hex_digit_value(body[index + length]) >= 0) {
            value = value * 16U + static_cast<unsigned>(hex_digit_value(body[index + length]));
            if (value > 0xFFU) {
                return 0;
            }
            ++length;
        }
        if (length == 1) {
            return 0;
        }
        decoded.push_back(static_cast<char>(value));
        return length;
    }
    if (ch == 'u' || ch == 'U') {
        const std::size_t digits = ch == 'u' ? 4 : 8;
        if (index + digits >= body.size()) {
            return 0;
        }
        std::uint32_t code_point = 0;
        for (std::size_t i = 1; i <= digits; ++i) {
            const int digit = hex_digit_value(body[index + i]);
            if (digit < 0) {
                return 0;
            }
            code_point = code_point * 16U + static_cast<std::uint32_t>(digit);
        }
        if (code_point > 0x10FFFFU || (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            return 0;
        }
        append_utf8(decoded, code_point);
        return digits + 1;
    }
    return 0;
}

} // namespace

std::string decode_string_literal(std::string_view value) {
    std::string trimmed = trim_copy(value);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        const std::string_view body{trimmed.data() + 1, trimmed.size() - 2};
        std::string            decoded;
        decoded.reserve(body.size());
        for (std::size_t idx = 0; idx < body.size();) {
            const char ch = body[idx];
            if (ch != '\\' || idx + 1 >= body.size()) {
                decoded.push_back(ch);
                ++idx;
                continue;
            }
            const std::size_t consumed = decode_escape(body, idx + 1, decoded);
            if (consumed == 0) {
                decoded.push_back('\\');
                ++idx;
                continue;
            }
            idx += 1 + consumed;
        }
        return decoded;
    }
    return trimmed;
}

namespace {

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

std::size_t raw_string_end(std::string_view text, std::size_t index, const RawStringStart &start) {
    const auto end = text.find(")" + start.delimiter + "\"", index + start.prefix_length);
    return end == std::string_view::npos ? text.size() : end + start.delimiter.size() + 2;
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

        if (const auto raw_start = detect_raw_string_start(text, idx)) {
            idx = raw_string_end(text, idx, *raw_start) - 1;
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

} // namespace

std::vector<std::string> split_expression_list(std::string_view arguments) {
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

        if (const auto raw_start = detect_raw_string_start(arguments, idx)) {
            const auto end = raw_string_end(arguments, idx, *raw_start);
            current.append(arguments.substr(idx, end - idx));
            idx = end - 1;
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
                    parts.push_back(std::move(token));
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
        parts.push_back(std::move(token));
    }
    return parts;
}

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
                        args        = split_expression_list(inside);
                        ++index; // consume ')'
                        break;
                    }
                }
            }
        }

        auto argument_tokens = args;
        for (auto &arg : args) {
            arg = decode_string_literal(arg);
        }
        attributes.push_back(
            ParsedAttribute{.name = std::move(name), .arguments = std::move(args), .argument_tokens = std::move(argument_tokens)});

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
