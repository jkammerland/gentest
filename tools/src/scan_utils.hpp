#pragma once

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gentest::codegen::scan {

inline std::string_view trim_ascii_view(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(begin, end - begin);
}

inline std::string_view ltrim_ascii_view(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    return text.substr(begin);
}

inline std::string_view rtrim_ascii_view(std::string_view text) {
    std::size_t end = text.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(0, end);
}

inline std::string trim_ascii_copy(std::string_view text) {
    return std::string{trim_ascii_view(text)};
}

inline std::string ltrim_ascii_copy(std::string_view text) {
    return std::string{ltrim_ascii_view(text)};
}

inline std::string rtrim_ascii_copy(std::string_view text) {
    return std::string{rtrim_ascii_view(text)};
}

inline std::string strip_comments_for_line_scan(std::string_view line, bool &in_block_comment) {
    std::string out;
    out.reserve(line.size());

    auto append_gap = [&]() {
        if (!out.empty() && !std::isspace(static_cast<unsigned char>(out.back()))) {
            out.push_back(' ');
        }
    };

    for (std::size_t i = 0; i < line.size();) {
        if (in_block_comment) {
            const auto end = line.find("*/", i);
            if (end == std::string_view::npos) {
                return out;
            }
            in_block_comment = false;
            append_gap();
            i = end + 2;
            continue;
        }
        if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
            append_gap();
            in_block_comment = true;
            i += 2;
            continue;
        }
        if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '/') {
            break;
        }
        out.push_back(line[i]);
        ++i;
    }
    return out;
}

inline std::string normalize_scan_directive_line(std::string_view line) {
    std::string out;
    out.reserve(line.size());

    bool pending_space = false;
    for (const unsigned char ch : line) {
        if (std::isspace(ch)) {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space && !out.empty()) {
            out.push_back(' ');
        }
        out.push_back(static_cast<char>(ch));
        pending_space = false;
    }
    return out;
}

inline bool has_trailing_line_continuation(std::string_view line) {
    line = rtrim_ascii_view(line);
    return !line.empty() && line.back() == '\\';
}

inline void strip_trailing_line_continuation(std::string &line) {
    line = rtrim_ascii_copy(line);
    if (!line.empty() && line.back() == '\\') {
        line.pop_back();
    }
    line = rtrim_ascii_copy(line);
}

inline bool consume_scan_keyword(std::string_view &cursor, std::string_view keyword) {
    const auto first = cursor.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        cursor = std::string_view{};
        return false;
    }
    cursor.remove_prefix(first);
    if (!cursor.starts_with(keyword)) {
        return false;
    }
    if (cursor.size() > keyword.size()) {
        const unsigned char next = static_cast<unsigned char>(cursor[keyword.size()]);
        if (!std::isspace(next) && next != ';' && next != '<' && next != '"' && next != ':') {
            return false;
        }
    }
    cursor.remove_prefix(keyword.size());
    return true;
}

struct ScanConditionalFrame {
    bool parent_active = true;
    bool branch_taken = false;
    bool active = true;
};

struct ScanStreamState {
    bool in_block_comment = false;
    bool in_preprocessor_continuation = false;
    bool current_branch_active = true;
    std::string pending_preprocessor;
    std::unordered_map<std::string, std::string> object_like_macros;
    std::vector<ScanConditionalFrame> conditionals;
};

struct ProcessedScanLine {
    bool is_active_code = false;
    bool is_preprocessor = false;
    std::string stripped;
};

inline bool is_scan_identifier_start(unsigned char ch) {
    return std::isalpha(ch) || ch == '_';
}

inline bool is_scan_identifier_continue(unsigned char ch) {
    return std::isalnum(ch) || ch == '_';
}

inline std::optional<std::string> parse_scan_identifier(std::string_view text) {
    text = trim_ascii_view(text);
    if (text.empty() || !is_scan_identifier_start(static_cast<unsigned char>(text.front()))) {
        return std::nullopt;
    }

    std::size_t end = 1;
    while (end < text.size() && is_scan_identifier_continue(static_cast<unsigned char>(text[end]))) {
        ++end;
    }
    return std::string{text.substr(0, end)};
}

inline std::optional<long long> parse_scan_integer_literal(std::string_view text) {
    text = trim_ascii_view(text);
    if (text.empty()) {
        return std::nullopt;
    }

    int sign = 1;
    if (text.front() == '+') {
        text.remove_prefix(1);
    } else if (text.front() == '-') {
        sign = -1;
        text.remove_prefix(1);
    }
    text = trim_ascii_view(text);
    if (text.empty()) {
        return std::nullopt;
    }

    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    if (text.empty()) {
        return std::nullopt;
    }

    long long value = 0;
    for (char ch : text) {
        int digit = -1;
        if (ch >= '0' && ch <= '9') {
            digit = ch - '0';
        } else if (base == 16 && ch >= 'a' && ch <= 'f') {
            digit = 10 + (ch - 'a');
        } else if (base == 16 && ch >= 'A' && ch <= 'F') {
            digit = 10 + (ch - 'A');
        } else {
            return std::nullopt;
        }
        if (digit >= base) {
            return std::nullopt;
        }
        value = value * base + digit;
    }
    return sign * value;
}

enum class ScanPpTokenKind {
    Identifier,
    Number,
    LParen,
    RParen,
    LogicalOr,
    LogicalAnd,
    BitOr,
    BitXor,
    BitAnd,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    ShiftLeft,
    ShiftRight,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Not,
    BitNot,
    End,
};

struct ScanPpToken {
    ScanPpTokenKind kind = ScanPpTokenKind::End;
    std::string     text;
};

inline std::vector<ScanPpToken> tokenize_scan_preprocessor_expression(std::string_view text) {
    std::vector<ScanPpToken> tokens;
    std::size_t              i = 0;
    while (i < text.size()) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (std::isspace(ch)) {
            ++i;
            continue;
        }

        if (is_scan_identifier_start(ch)) {
            const std::size_t begin = i++;
            while (i < text.size() && is_scan_identifier_continue(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            tokens.push_back(ScanPpToken{ScanPpTokenKind::Identifier, std::string{text.substr(begin, i - begin)}});
            continue;
        }

        if (std::isdigit(ch)) {
            const std::size_t begin = i++;
            while (i < text.size()) {
                const unsigned char next = static_cast<unsigned char>(text[i]);
                if (!std::isalnum(next) && next != '_') {
                    break;
                }
                ++i;
            }
            tokens.push_back(ScanPpToken{ScanPpTokenKind::Number, std::string{text.substr(begin, i - begin)}});
            continue;
        }

        auto push = [&](ScanPpTokenKind kind, std::size_t len) {
            tokens.push_back(ScanPpToken{kind, std::string{text.substr(i, len)}});
            i += len;
        };

        if (i + 1 < text.size()) {
            const std::string_view two = text.substr(i, 2);
            if (two == "||") {
                push(ScanPpTokenKind::LogicalOr, 2);
                continue;
            }
            if (two == "&&") {
                push(ScanPpTokenKind::LogicalAnd, 2);
                continue;
            }
            if (two == "==") {
                push(ScanPpTokenKind::Equal, 2);
                continue;
            }
            if (two == "!=") {
                push(ScanPpTokenKind::NotEqual, 2);
                continue;
            }
            if (two == "<=") {
                push(ScanPpTokenKind::LessEqual, 2);
                continue;
            }
            if (two == ">=") {
                push(ScanPpTokenKind::GreaterEqual, 2);
                continue;
            }
            if (two == "<<") {
                push(ScanPpTokenKind::ShiftLeft, 2);
                continue;
            }
            if (two == ">>") {
                push(ScanPpTokenKind::ShiftRight, 2);
                continue;
            }
        }

        switch (text[i]) {
        case '(':
            push(ScanPpTokenKind::LParen, 1);
            break;
        case ')':
            push(ScanPpTokenKind::RParen, 1);
            break;
        case '|':
            push(ScanPpTokenKind::BitOr, 1);
            break;
        case '^':
            push(ScanPpTokenKind::BitXor, 1);
            break;
        case '&':
            push(ScanPpTokenKind::BitAnd, 1);
            break;
        case '<':
            push(ScanPpTokenKind::Less, 1);
            break;
        case '>':
            push(ScanPpTokenKind::Greater, 1);
            break;
        case '+':
            push(ScanPpTokenKind::Plus, 1);
            break;
        case '-':
            push(ScanPpTokenKind::Minus, 1);
            break;
        case '*':
            push(ScanPpTokenKind::Star, 1);
            break;
        case '/':
            push(ScanPpTokenKind::Slash, 1);
            break;
        case '%':
            push(ScanPpTokenKind::Percent, 1);
            break;
        case '!':
            push(ScanPpTokenKind::Not, 1);
            break;
        case '~':
            push(ScanPpTokenKind::BitNot, 1);
            break;
        default:
            return {};
        }
    }
    tokens.push_back(ScanPpToken{ScanPpTokenKind::End, {}});
    return tokens;
}

class ScanPpExpressionParser {
public:
    ScanPpExpressionParser(std::string_view text, const std::unordered_map<std::string, std::string> &object_like_macros,
                           std::size_t depth = 0)
        : tokens_(tokenize_scan_preprocessor_expression(text)), object_like_macros_(object_like_macros), depth_(depth) {}

    std::optional<long long> parse() {
        if (tokens_.empty()) {
            return std::nullopt;
        }
        const auto value = parse_logical_or();
        if (!value.has_value() || current().kind != ScanPpTokenKind::End) {
            return std::nullopt;
        }
        return value;
    }

private:
    const ScanPpToken &current() const {
        static const ScanPpToken kEnd{};
        if (index_ >= tokens_.size()) {
            return kEnd;
        }
        return tokens_[index_];
    }

    bool consume(ScanPpTokenKind kind) {
        if (current().kind != kind) {
            return false;
        }
        ++index_;
        return true;
    }

    std::optional<long long> parse_logical_or() {
        auto lhs = parse_logical_and();
        while (lhs.has_value() && consume(ScanPpTokenKind::LogicalOr)) {
            auto rhs = parse_logical_and();
            if (!rhs.has_value()) {
                return std::nullopt;
            }
            lhs = ((*lhs != 0) || (*rhs != 0)) ? 1ll : 0ll;
        }
        return lhs;
    }

    std::optional<long long> parse_logical_and() {
        auto lhs = parse_bitwise_or();
        while (lhs.has_value() && consume(ScanPpTokenKind::LogicalAnd)) {
            auto rhs = parse_bitwise_or();
            if (!rhs.has_value()) {
                return std::nullopt;
            }
            lhs = ((*lhs != 0) && (*rhs != 0)) ? 1ll : 0ll;
        }
        return lhs;
    }

    std::optional<long long> parse_bitwise_or() {
        auto lhs = parse_bitwise_xor();
        while (lhs.has_value() && consume(ScanPpTokenKind::BitOr)) {
            auto rhs = parse_bitwise_xor();
            if (!rhs.has_value()) {
                return std::nullopt;
            }
            lhs = *lhs | *rhs;
        }
        return lhs;
    }

    std::optional<long long> parse_bitwise_xor() {
        auto lhs = parse_bitwise_and();
        while (lhs.has_value() && consume(ScanPpTokenKind::BitXor)) {
            auto rhs = parse_bitwise_and();
            if (!rhs.has_value()) {
                return std::nullopt;
            }
            lhs = *lhs ^ *rhs;
        }
        return lhs;
    }

    std::optional<long long> parse_bitwise_and() {
        auto lhs = parse_equality();
        while (lhs.has_value() && consume(ScanPpTokenKind::BitAnd)) {
            auto rhs = parse_equality();
            if (!rhs.has_value()) {
                return std::nullopt;
            }
            lhs = *lhs & *rhs;
        }
        return lhs;
    }

    std::optional<long long> parse_equality() {
        auto lhs = parse_relational();
        while (lhs.has_value()) {
            if (consume(ScanPpTokenKind::Equal)) {
                auto rhs = parse_relational();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = (*lhs == *rhs) ? 1ll : 0ll;
                continue;
            }
            if (consume(ScanPpTokenKind::NotEqual)) {
                auto rhs = parse_relational();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = (*lhs != *rhs) ? 1ll : 0ll;
                continue;
            }
            break;
        }
        return lhs;
    }

    std::optional<long long> parse_relational() {
        auto lhs = parse_shift();
        while (lhs.has_value()) {
            if (consume(ScanPpTokenKind::Less)) {
                auto rhs = parse_shift();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = (*lhs < *rhs) ? 1ll : 0ll;
                continue;
            }
            if (consume(ScanPpTokenKind::LessEqual)) {
                auto rhs = parse_shift();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = (*lhs <= *rhs) ? 1ll : 0ll;
                continue;
            }
            if (consume(ScanPpTokenKind::Greater)) {
                auto rhs = parse_shift();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = (*lhs > *rhs) ? 1ll : 0ll;
                continue;
            }
            if (consume(ScanPpTokenKind::GreaterEqual)) {
                auto rhs = parse_shift();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = (*lhs >= *rhs) ? 1ll : 0ll;
                continue;
            }
            break;
        }
        return lhs;
    }

    std::optional<long long> parse_shift() {
        auto lhs = parse_additive();
        while (lhs.has_value()) {
            if (consume(ScanPpTokenKind::ShiftLeft)) {
                auto rhs = parse_additive();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = *lhs << *rhs;
                continue;
            }
            if (consume(ScanPpTokenKind::ShiftRight)) {
                auto rhs = parse_additive();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = *lhs >> *rhs;
                continue;
            }
            break;
        }
        return lhs;
    }

    std::optional<long long> parse_additive() {
        auto lhs = parse_multiplicative();
        while (lhs.has_value()) {
            if (consume(ScanPpTokenKind::Plus)) {
                auto rhs = parse_multiplicative();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = *lhs + *rhs;
                continue;
            }
            if (consume(ScanPpTokenKind::Minus)) {
                auto rhs = parse_multiplicative();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = *lhs - *rhs;
                continue;
            }
            break;
        }
        return lhs;
    }

    std::optional<long long> parse_multiplicative() {
        auto lhs = parse_unary();
        while (lhs.has_value()) {
            if (consume(ScanPpTokenKind::Star)) {
                auto rhs = parse_unary();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = *lhs * *rhs;
                continue;
            }
            if (consume(ScanPpTokenKind::Slash)) {
                auto rhs = parse_unary();
                if (!rhs.has_value() || *rhs == 0) {
                    return std::nullopt;
                }
                lhs = *lhs / *rhs;
                continue;
            }
            if (consume(ScanPpTokenKind::Percent)) {
                auto rhs = parse_unary();
                if (!rhs.has_value() || *rhs == 0) {
                    return std::nullopt;
                }
                lhs = *lhs % *rhs;
                continue;
            }
            break;
        }
        return lhs;
    }

    std::optional<long long> parse_unary() {
        if (consume(ScanPpTokenKind::Not)) {
            auto value = parse_unary();
            if (!value.has_value()) {
                return std::nullopt;
            }
            return (*value == 0) ? 1ll : 0ll;
        }
        if (consume(ScanPpTokenKind::BitNot)) {
            auto value = parse_unary();
            if (!value.has_value()) {
                return std::nullopt;
            }
            return ~*value;
        }
        if (consume(ScanPpTokenKind::Plus)) {
            return parse_unary();
        }
        if (consume(ScanPpTokenKind::Minus)) {
            auto value = parse_unary();
            if (!value.has_value()) {
                return std::nullopt;
            }
            return -*value;
        }
        return parse_primary();
    }

    std::optional<long long> parse_primary() {
        if (consume(ScanPpTokenKind::LParen)) {
            auto value = parse_logical_or();
            if (!value.has_value() || !consume(ScanPpTokenKind::RParen)) {
                return std::nullopt;
            }
            return value;
        }

        if (current().kind == ScanPpTokenKind::Identifier && current().text == "defined") {
            ++index_;
            bool parenthesized = consume(ScanPpTokenKind::LParen);
            if (current().kind != ScanPpTokenKind::Identifier) {
                return std::nullopt;
            }
            const std::string ident = current().text;
            ++index_;
            if (parenthesized && !consume(ScanPpTokenKind::RParen)) {
                return std::nullopt;
            }
            return object_like_macros_.contains(ident) ? 1ll : 0ll;
        }

        if (current().kind == ScanPpTokenKind::Number) {
            const auto value = parse_scan_integer_literal(current().text);
            ++index_;
            return value;
        }

        if (current().kind == ScanPpTokenKind::Identifier) {
            const std::string ident = current().text;
            ++index_;
            if (const auto it = object_like_macros_.find(ident); it != object_like_macros_.end()) {
                const auto macro_value = trim_ascii_view(it->second);
                if (macro_value.empty()) {
                    return 1ll;
                }
                if (depth_ >= 32) {
                    return std::nullopt;
                }
                ScanPpExpressionParser nested{macro_value, object_like_macros_, depth_ + 1};
                return nested.parse();
            }
            return 0ll;
        }

        return std::nullopt;
    }

    const std::vector<ScanPpToken>                     tokens_;
    const std::unordered_map<std::string, std::string> &object_like_macros_;
    std::size_t                                        index_ = 0;
    std::size_t                                        depth_ = 0;
};

inline std::optional<bool> evaluate_simple_preprocessor_condition(
    std::string_view text, const std::unordered_map<std::string, std::string> &object_like_macros) {
    text = trim_ascii_view(text);
    if (text.empty()) {
        return std::nullopt;
    }
    ScanPpExpressionParser parser{text, object_like_macros};
    if (const auto value = parser.parse(); value.has_value()) {
        return *value != 0;
    }
    return std::nullopt;
}

inline void update_preprocessor_branch_state(ScanStreamState &state) {
    state.current_branch_active = state.conditionals.empty() ? true : state.conditionals.back().active;
}

inline void handle_preprocessor_logical_line(std::string_view raw_line, ScanStreamState &state) {
    std::string_view cursor = ltrim_ascii_view(raw_line);
    if (cursor.empty() || cursor.front() != '#') {
        return;
    }
    cursor.remove_prefix(1);
    cursor = ltrim_ascii_view(cursor);

    std::size_t keyword_end = 0;
    while (keyword_end < cursor.size() && std::isalpha(static_cast<unsigned char>(cursor[keyword_end]))) {
        ++keyword_end;
    }
    const std::string_view keyword = cursor.substr(0, keyword_end);
    const std::string_view rest = trim_ascii_view(cursor.substr(keyword_end));

    if (keyword == "if") {
        const bool parent_active = state.current_branch_active;
        const auto evaluated = evaluate_simple_preprocessor_condition(rest, state.object_like_macros);
        const bool branch_active = parent_active && evaluated.value_or(true);
        state.conditionals.push_back(ScanConditionalFrame{
            .parent_active = parent_active,
            .branch_taken = branch_active,
            .active = branch_active,
        });
        update_preprocessor_branch_state(state);
        return;
    }

    if (keyword == "ifdef" || keyword == "ifndef") {
        const bool parent_active = state.current_branch_active;
        const auto ident = parse_scan_identifier(rest);
        const bool defined = ident.has_value() && state.object_like_macros.contains(*ident);
        const bool branch_active = parent_active && (keyword == "ifdef" ? defined : !defined);
        state.conditionals.push_back(ScanConditionalFrame{
            .parent_active = parent_active,
            .branch_taken = branch_active,
            .active = branch_active,
        });
        update_preprocessor_branch_state(state);
        return;
    }

    if (keyword == "elif") {
        if (state.conditionals.empty()) {
            return;
        }
        auto &frame = state.conditionals.back();
        const auto evaluated = evaluate_simple_preprocessor_condition(rest, state.object_like_macros);
        const bool branch_active = frame.parent_active && !frame.branch_taken && evaluated.value_or(true);
        frame.active = branch_active;
        frame.branch_taken = frame.branch_taken || branch_active;
        update_preprocessor_branch_state(state);
        return;
    }

    if (keyword == "else") {
        if (state.conditionals.empty()) {
            return;
        }
        auto &frame = state.conditionals.back();
        const bool branch_active = frame.parent_active && !frame.branch_taken;
        frame.active = branch_active;
        frame.branch_taken = true;
        update_preprocessor_branch_state(state);
        return;
    }

    if (keyword == "endif") {
        if (!state.conditionals.empty()) {
            state.conditionals.pop_back();
        }
        update_preprocessor_branch_state(state);
        return;
    }

    if (!state.current_branch_active) {
        return;
    }

    if (keyword == "define") {
        const auto ident = parse_scan_identifier(rest);
        if (!ident.has_value()) {
            return;
        }
        std::string_view remainder = trim_ascii_view(rest.substr(ident->size()));
        if (!remainder.empty() && remainder.front() == '(') {
            return;
        }
        state.object_like_macros[*ident] = std::string{remainder};
        return;
    }

    if (keyword == "undef") {
        const auto ident = parse_scan_identifier(rest);
        if (ident.has_value()) {
            state.object_like_macros.erase(*ident);
        }
    }
}

inline ProcessedScanLine process_scan_physical_line(std::string_view raw_line, ScanStreamState &state) {
    ProcessedScanLine processed;
    const std::string stripped = strip_comments_for_line_scan(raw_line, state.in_block_comment);
    const std::string_view trimmed = trim_ascii_view(stripped);
    const bool is_preprocessor = state.in_preprocessor_continuation || (!trimmed.empty() && trimmed.front() == '#');

    if (is_preprocessor) {
        processed.is_preprocessor = true;
        if (!trimmed.empty()) {
            if (!state.pending_preprocessor.empty()) {
                state.pending_preprocessor.push_back(' ');
            }
            state.pending_preprocessor.append(trimmed);
        }

        state.in_preprocessor_continuation = has_trailing_line_continuation(trimmed);
        if (state.in_preprocessor_continuation) {
            strip_trailing_line_continuation(state.pending_preprocessor);
        } else if (!state.pending_preprocessor.empty()) {
            handle_preprocessor_logical_line(state.pending_preprocessor, state);
            state.pending_preprocessor.clear();
        }
        return processed;
    }

    if (!state.current_branch_active) {
        return processed;
    }

    processed.stripped = std::string{trimmed};
    processed.is_active_code = !processed.stripped.empty();
    return processed;
}

inline std::vector<std::string> split_scan_statements(std::string_view line) {
    std::vector<std::string> statements;
    std::string current;
    current.reserve(line.size());
    for (char ch : line) {
        current.push_back(ch);
        if (ch == ';') {
            const std::string trimmed = trim_ascii_copy(current);
            if (!trimmed.empty()) {
                statements.push_back(trimmed);
            }
            current.clear();
        }
    }

    const std::string trailing = trim_ascii_copy(current);
    if (!trailing.empty()) {
        statements.push_back(trailing);
    }
    return statements;
}

inline bool is_preprocessor_directive_scan_line(std::string_view line) {
    return trim_ascii_copy(line).starts_with('#');
}

inline bool is_global_module_fragment_scan_line(std::string_view line) {
    return normalize_scan_directive_line(line) == "module;";
}

inline bool looks_like_named_module_scan_prefix(std::string_view line) {
    const std::string normalized = normalize_scan_directive_line(line);
    std::string_view  cursor     = normalized;
    if (consume_scan_keyword(cursor, "export")) {
        cursor = ltrim_ascii_view(cursor);
        if (cursor.empty()) {
            return true;
        }
    }

    if (cursor == "module") {
        return true;
    }
    if (!consume_scan_keyword(cursor, "module")) {
        return false;
    }
    return true;
}

inline bool looks_like_import_scan_prefix(std::string_view line) {
    const std::string normalized = normalize_scan_directive_line(line);
    std::string_view  cursor     = normalized;
    if (consume_scan_keyword(cursor, "export")) {
        cursor = ltrim_ascii_view(cursor);
        if (cursor.empty()) {
            return true;
        }
    }

    if (cursor == "import") {
        return true;
    }
    if (!consume_scan_keyword(cursor, "import")) {
        return false;
    }
    return true;
}

inline std::optional<std::string> parse_named_module_name_from_scan_line(std::string_view line) {
    const std::string normalized = normalize_scan_directive_line(line);
    std::string_view  cursor     = normalized;
    if (consume_scan_keyword(cursor, "export")) {
        cursor = ltrim_ascii_view(cursor);
    }
    if (!consume_scan_keyword(cursor, "module")) {
        return std::nullopt;
    }

    cursor = ltrim_ascii_view(cursor);
    if (cursor.empty() || cursor.front() == ';') {
        return std::nullopt;
    }

    const auto semi = cursor.find(';');
    if (semi == std::string_view::npos) {
        return std::nullopt;
    }

    std::string module_name = trim_ascii_copy(cursor.substr(0, semi));
    if (module_name.empty()) {
        return std::nullopt;
    }
    return module_name;
}

inline bool is_any_import_scan_line(std::string_view line) {
    const std::string normalized = normalize_scan_directive_line(line);
    std::string_view  cursor     = normalized;
    if (consume_scan_keyword(cursor, "export")) {
        cursor = ltrim_ascii_view(cursor);
    }
    if (!consume_scan_keyword(cursor, "import")) {
        return false;
    }

    cursor = ltrim_ascii_view(cursor);
    if (cursor.empty()) {
        return false;
    }

    return cursor.find(';') != std::string_view::npos;
}

inline std::optional<std::string> parse_imported_module_name_from_scan_line(std::string_view line) {
    if (!is_any_import_scan_line(line)) {
        return std::nullopt;
    }

    const std::string normalized = normalize_scan_directive_line(line);
    std::string_view  cursor     = normalized;
    if (consume_scan_keyword(cursor, "export")) {
        cursor = ltrim_ascii_view(cursor);
    }
    if (!consume_scan_keyword(cursor, "import")) {
        return std::nullopt;
    }

    cursor = ltrim_ascii_view(cursor);
    if (cursor.empty() || cursor.front() == '<' || cursor.front() == '"') {
        return std::nullopt;
    }

    const auto semi = cursor.find(';');
    if (semi == std::string_view::npos) {
        return std::nullopt;
    }

    std::string module_name = trim_ascii_copy(cursor.substr(0, semi));
    if (module_name.empty()) {
        return std::nullopt;
    }
    return module_name;
}

inline std::optional<std::string> parse_include_header_from_scan_line(std::string_view line) {
    std::string trimmed = trim_ascii_copy(line);
    if (trimmed.empty() || trimmed.front() != '#') {
        return std::nullopt;
    }

    std::string_view cursor = trimmed;
    cursor.remove_prefix(1);
    const std::string after_hash = ltrim_ascii_copy(cursor);
    cursor                       = after_hash;
    if (!cursor.starts_with("include")) {
        return std::nullopt;
    }
    if (cursor.size() > std::string_view{"include"}.size()) {
        const unsigned char next = static_cast<unsigned char>(cursor[std::string_view{"include"}.size()]);
        if (!std::isspace(next) && next != '<' && next != '"') {
            return std::nullopt;
        }
    }
    cursor.remove_prefix(std::string_view{"include"}.size());
    const std::string after_include = ltrim_ascii_copy(cursor);
    cursor                          = after_include;
    if (cursor.empty()) {
        return std::nullopt;
    }

    const char open = cursor.front();
    const char close = open == '<' ? '>' : (open == '"' ? '"' : '\0');
    if (close == '\0') {
        return std::nullopt;
    }
    const auto end = cursor.find(close, 1);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string{cursor.substr(1, end - 1)};
}

inline std::optional<std::string> named_module_name_from_source_file(const std::filesystem::path &path) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }

    ScanStreamState state;
    std::string line;
    std::string pending;
    bool        pending_active = false;
    while (std::getline(in, line)) {
        const auto processed = process_scan_physical_line(line, state);
        if (!processed.is_active_code) {
            continue;
        }

        for (const auto &statement : split_scan_statements(processed.stripped)) {
            if (!pending_active) {
                if (!looks_like_named_module_scan_prefix(statement) && !is_global_module_fragment_scan_line(statement)) {
                    continue;
                }
                pending = statement;
                pending_active = true;
            } else {
                pending.push_back(' ');
                pending.append(statement);
            }

            if (statement.find(';') == std::string::npos) {
                if (!looks_like_named_module_scan_prefix(pending) && !is_global_module_fragment_scan_line(pending)) {
                    pending.clear();
                    pending_active = false;
                }
                continue;
            }

            if (auto module_name = parse_named_module_name_from_scan_line(pending); module_name.has_value()) {
                return module_name;
            }

            pending.clear();
            pending_active = false;
        }
    }
    return std::nullopt;
}

} // namespace gentest::codegen::scan
