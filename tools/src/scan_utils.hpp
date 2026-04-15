#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

inline std::string trim_ascii_copy(std::string_view text) { return std::string{trim_ascii_view(text)}; }

inline std::string ltrim_ascii_copy(std::string_view text) { return std::string{ltrim_ascii_view(text)}; }

inline std::string rtrim_ascii_copy(std::string_view text) { return std::string{rtrim_ascii_view(text)}; }

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

inline std::string to_lower_ascii_copy(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char ch : text) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

inline std::optional<std::string> read_scan_env_value(std::string_view env_name) {
    std::string env_name_str{env_name};
#if defined(_WIN32)
    char  *env_value = nullptr;
    size_t env_len   = 0;
    if (_dupenv_s(&env_value, &env_len, env_name_str.c_str()) != 0 || env_value == nullptr) {
        return std::nullopt;
    }
    std::string value{env_value};
    std::free(env_value);
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
#else
    const char *env_value = std::getenv(env_name_str.c_str());
    if (!env_value || !*env_value) {
        return std::nullopt;
    }
    return std::string{env_value};
#endif
}

inline std::vector<std::filesystem::path> split_scan_env_paths(const char *env_name) {
    std::vector<std::filesystem::path> paths;
    const auto                         raw = read_scan_env_value(env_name);
    if (!raw.has_value()) {
        return paths;
    }

#if defined(_WIN32)
    constexpr char kSeparator = ';';
#else
    constexpr char kSeparator = ':';
#endif

    std::string_view remaining{*raw};
    while (!remaining.empty()) {
        const auto split   = remaining.find(kSeparator);
        const auto piece   = split == std::string_view::npos ? remaining : remaining.substr(0, split);
        const auto trimmed = trim_ascii_view(piece);
        if (!trimmed.empty()) {
            paths.emplace_back(std::string(trimmed));
        }
        if (split == std::string_view::npos) {
            break;
        }
        remaining.remove_prefix(split + 1);
    }
    return paths;
}

inline void append_unique_scan_path(std::vector<std::filesystem::path> &paths, const std::filesystem::path &path) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec) || !std::filesystem::is_directory(path, ec)) {
        return;
    }

    const std::filesystem::path normalized = path.lexically_normal();
    if (std::ranges::find(paths, normalized) == paths.end()) {
        paths.push_back(normalized);
    }
}

inline void append_scan_subdirectories(std::vector<std::filesystem::path> &paths, const std::filesystem::path &root) {
    std::error_code ec;
    if (root.empty() || !std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        return;
    }

    for (const auto &entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_directory(ec)) {
            append_unique_scan_path(paths, entry.path());
        }
    }
}

inline std::vector<std::filesystem::path> default_scan_include_search_paths(const std::filesystem::path              &source_directory,
                                                                            const std::vector<std::filesystem::path> &extra_paths = {}) {
    std::vector<std::filesystem::path> paths;
    append_unique_scan_path(paths, source_directory);
    for (const auto &path : extra_paths) {
        append_unique_scan_path(paths, path);
    }

    for (const char *env_name : {"CPATH", "CPLUS_INCLUDE_PATH", "C_INCLUDE_PATH"}) {
        for (const auto &path : split_scan_env_paths(env_name)) {
            append_unique_scan_path(paths, path);
        }
    }

#if defined(__linux__)
    append_unique_scan_path(paths, "/usr/local/include");
    append_unique_scan_path(paths, "/usr/include");
    append_unique_scan_path(paths, "/usr/include/c++");
    append_scan_subdirectories(paths, "/usr/include/c++");
    std::error_code ec;
    if (std::filesystem::exists("/usr/include", ec)) {
        for (const auto &entry : std::filesystem::directory_iterator("/usr/include", ec)) {
            if (ec || !entry.is_directory(ec)) {
                continue;
            }
            const auto name = entry.path().filename().string();
            if (name.find("-linux-gnu") != std::string::npos) {
                append_unique_scan_path(paths, entry.path() / "c++");
                append_scan_subdirectories(paths, entry.path() / "c++");
            }
        }
    }
    append_unique_scan_path(paths, "/usr/lib/clang");
    append_scan_subdirectories(paths, "/usr/lib/clang");
    std::vector<std::filesystem::path> clang_roots;
    clang_roots.reserve(paths.size());
    for (const auto &path : paths) {
        if (path.filename() == "clang") {
            clang_roots.push_back(path);
        }
    }
    for (const auto &clang_dir : clang_roots) {
        append_scan_subdirectories(paths, clang_dir);
    }
    std::vector<std::filesystem::path> clang_include_dirs;
    for (const auto &path : paths) {
        if (path.filename() == "include" && path.parent_path().filename() == "clang") {
            continue;
        }
        if (path.parent_path().filename() == "clang") {
            append_unique_scan_path(clang_include_dirs, path / "include");
        }
    }
    for (const auto &path : clang_include_dirs) {
        append_unique_scan_path(paths, path);
    }
#elif defined(__APPLE__)
    append_unique_scan_path(paths, "/usr/local/include");
    append_unique_scan_path(paths, "/opt/homebrew/include");
    append_unique_scan_path(paths, "/opt/homebrew/opt/llvm/include/c++/v1");
    append_unique_scan_path(paths, "/usr/include/c++/v1");
    append_unique_scan_path(paths, "/Library/Developer/CommandLineTools/usr/include/c++/v1");
    if (const auto sdkroot = read_scan_env_value("SDKROOT"); sdkroot && !sdkroot->empty()) {
        append_unique_scan_path(paths, std::filesystem::path{*sdkroot} / "usr/include");
        append_unique_scan_path(paths, std::filesystem::path{*sdkroot} / "usr/include/c++/v1");
    }
#endif

    return paths;
}

inline bool is_valid_scan_module_component(std::string_view text) {
    const auto is_ident_start    = [](unsigned char ch) { return std::isalpha(ch) || ch == '_'; };
    const auto is_ident_continue = [](unsigned char ch) { return std::isalnum(ch) || ch == '_'; };
    text                         = trim_ascii_view(text);
    if (text.empty()) {
        return false;
    }

    std::size_t begin = 0;
    while (begin < text.size()) {
        std::size_t end = begin;
        while (end < text.size() && text[end] != '.') {
            ++end;
        }
        const auto part = text.substr(begin, end - begin);
        if (part.empty() || !is_ident_start(static_cast<unsigned char>(part.front()))) {
            return false;
        }
        for (std::size_t i = 1; i < part.size(); ++i) {
            if (!is_ident_continue(static_cast<unsigned char>(part[i]))) {
                return false;
            }
        }
        begin = end == text.size() ? end : end + 1;
    }
    return true;
}

inline std::optional<std::string> canonicalize_scan_module_name(std::string_view text, bool allow_partition_only) {
    text = trim_ascii_view(text);
    if (text.empty()) {
        return std::nullopt;
    }

    const auto colon = text.find(':');
    if (colon == std::string_view::npos) {
        if (!is_valid_scan_module_component(text)) {
            return std::nullopt;
        }
        return std::string{text};
    }

    const auto extra_colon = text.find(':', colon + 1);
    if (extra_colon != std::string_view::npos) {
        return std::nullopt;
    }

    const auto primary   = trim_ascii_view(text.substr(0, colon));
    const auto partition = trim_ascii_view(text.substr(colon + 1));
    if (partition.empty() || !is_valid_scan_module_component(partition)) {
        return std::nullopt;
    }
    if (primary.empty()) {
        if (!allow_partition_only) {
            return std::nullopt;
        }
        return std::string{":"} + std::string(partition);
    }
    if (!is_valid_scan_module_component(primary)) {
        return std::nullopt;
    }
    return std::string(primary) + ":" + std::string(partition);
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
        const auto next = static_cast<unsigned char>(cursor[keyword.size()]);
        if (!std::isspace(next) && next != ';' && next != '<' && next != '"' && next != ':') {
            return false;
        }
    }
    cursor.remove_prefix(keyword.size());
    return true;
}

struct ScanConditionalFrame {
    bool parent_active = true;
    bool branch_taken  = false;
    bool active        = true;
};

struct ScanStreamState {
    bool                                                                                   in_block_comment             = false;
    bool                                                                                   in_preprocessor_continuation = false;
    bool                                                                                   current_branch_active        = true;
    std::string                                                                            pending_preprocessor;
    std::filesystem::path                                                                  source_path;
    std::filesystem::path                                                                  source_directory;
    std::vector<std::filesystem::path>                                                     include_search_paths;
    std::unordered_map<std::string, std::string>                                           object_like_macros;
    std::vector<ScanConditionalFrame>                                                      conditionals;
    bool                                                                                   warn_on_unknown_conditions      = true;
    std::size_t                                                                            current_line                    = 0;
    std::size_t                                                                            pending_preprocessor_start_line = 0;
    std::function<void(std::string_view, std::size_t, std::string_view, std::string_view)> unknown_condition_warning_sink;
};

struct ProcessedScanLine {
    bool        is_active_code  = false;
    bool        is_preprocessor = false;
    std::string stripped;
};

inline bool is_scan_identifier_start(unsigned char ch) { return std::isalpha(ch) || ch == '_'; }

inline bool is_scan_identifier_continue(unsigned char ch) { return std::isalnum(ch) || ch == '_'; }

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

inline void populate_scan_macros_from_command_line(ScanStreamState &state, std::span<const std::string> command_line) {
    auto define_macro = [&](std::string_view definition) {
        const auto eq   = definition.find('=');
        const auto name = eq == std::string_view::npos ? definition : definition.substr(0, eq);
        if (name.empty() || name.find('(') != std::string_view::npos) {
            return;
        }
        if (!parse_scan_identifier(name).has_value()) {
            return;
        }
        state.object_like_macros[std::string(name)] =
            eq == std::string_view::npos ? std::string{"1"} : std::string(definition.substr(eq + 1));
    };
    auto undefine_macro = [&](std::string_view definition) {
        if (definition.empty() || definition.find('(') != std::string_view::npos) {
            return;
        }
        if (const auto ident = parse_scan_identifier(definition); ident.has_value()) {
            state.object_like_macros.erase(*ident);
        }
    };

    bool consume_define = false;
    bool consume_undef  = false;
    for (const auto &arg : command_line) {
        if (consume_define) {
            define_macro(arg);
            consume_define = false;
            continue;
        }
        if (consume_undef) {
            undefine_macro(arg);
            consume_undef = false;
            continue;
        }

        if (arg == "-D" || arg == "/D") {
            consume_define = true;
            continue;
        }
        if (arg == "-U" || arg == "/U") {
            consume_undef = true;
            continue;
        }
        if (arg.starts_with("-D") || arg.starts_with("/D")) {
            define_macro(std::string_view(arg).substr(2));
            continue;
        }
        if (arg.starts_with("-U") || arg.starts_with("/U")) {
            undefine_macro(std::string_view(arg).substr(2));
        }
    }
}

inline std::optional<long long> parse_scan_integer_literal(std::string_view text) {
    text = trim_ascii_view(text);
    if (text.empty()) {
        return std::nullopt;
    }

    std::string normalized{text};
    normalized.erase(std::ranges::remove(normalized, '\'').begin(), normalized.end());
    text = normalized;

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
    } else if (text.size() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
        base = 2;
        text.remove_prefix(2);
    } else if (text.size() > 1 && text[0] == '0') {
        base = 8;
    }
    if (text.empty()) {
        return std::nullopt;
    }

    const auto is_valid_suffix = [](std::string_view suffix) {
        const std::string lowered = to_lower_ascii_copy(suffix);
        return lowered.empty() || lowered == "u" || lowered == "l" || lowered == "ll" || lowered == "ul" || lowered == "ull" ||
               lowered == "lu" || lowered == "llu";
    };

    std::size_t digits_end = 0;
    while (digits_end < text.size()) {
        const char ch       = text[digits_end];
        const bool is_digit = (base <= 10 && ch >= '0' && ch <= static_cast<char>('0' + (base - 1))) ||
                              (base == 16 && ch >= 'a' && ch <= 'f') || (base == 16 && ch >= 'A' && ch <= 'F');
        if (!is_digit) {
            break;
        }
        ++digits_end;
    }
    if (digits_end == 0 || !is_valid_suffix(text.substr(digits_end))) {
        return std::nullopt;
    }

    long long value = 0;
    for (std::size_t i = 0; i < digits_end; ++i) {
        const char ch    = text[i];
        int        digit = -1;
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
        const auto ch = static_cast<unsigned char>(text[i]);
        if (std::isspace(ch)) {
            ++i;
            continue;
        }

        if (is_scan_identifier_start(ch)) {
            const std::size_t begin = i++;
            while (i < text.size() && is_scan_identifier_continue(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            tokens.push_back(ScanPpToken{
                .kind = ScanPpTokenKind::Identifier,
                .text = std::string{text.substr(begin, i - begin)},
            });
            continue;
        }

        if (std::isdigit(ch)) {
            const std::size_t begin = i++;
            while (i < text.size()) {
                const auto next = static_cast<unsigned char>(text[i]);
                if (!std::isalnum(next) && next != '_' && next != '\'') {
                    break;
                }
                ++i;
            }
            tokens.push_back(ScanPpToken{
                .kind = ScanPpTokenKind::Number,
                .text = std::string{text.substr(begin, i - begin)},
            });
            continue;
        }

        auto push = [&](ScanPpTokenKind kind, std::size_t len) {
            tokens.push_back(ScanPpToken{
                .kind = kind,
                .text = std::string{text.substr(i, len)},
            });
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
        case '(': push(ScanPpTokenKind::LParen, 1); break;
        case ')': push(ScanPpTokenKind::RParen, 1); break;
        case '|': push(ScanPpTokenKind::BitOr, 1); break;
        case '^': push(ScanPpTokenKind::BitXor, 1); break;
        case '&': push(ScanPpTokenKind::BitAnd, 1); break;
        case '<': push(ScanPpTokenKind::Less, 1); break;
        case '>': push(ScanPpTokenKind::Greater, 1); break;
        case '+': push(ScanPpTokenKind::Plus, 1); break;
        case '-': push(ScanPpTokenKind::Minus, 1); break;
        case '*': push(ScanPpTokenKind::Star, 1); break;
        case '/': push(ScanPpTokenKind::Slash, 1); break;
        case '%': push(ScanPpTokenKind::Percent, 1); break;
        case '!': push(ScanPpTokenKind::Not, 1); break;
        case '~': push(ScanPpTokenKind::BitNot, 1); break;
        default: return {};
        }
    }
    tokens.push_back(ScanPpToken{
        .kind = ScanPpTokenKind::End,
        .text = {},
    });
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
            lhs = ((*lhs != 0) || (*rhs != 0)) ? 1LL : 0LL;
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
            lhs = ((*lhs != 0) && (*rhs != 0)) ? 1LL : 0LL;
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
                lhs = (*lhs == *rhs) ? 1LL : 0LL;
                continue;
            }
            if (consume(ScanPpTokenKind::NotEqual)) {
                auto rhs = parse_relational();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = (*lhs != *rhs) ? 1LL : 0LL;
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
                lhs = (*lhs < *rhs) ? 1LL : 0LL;
                continue;
            }
            if (consume(ScanPpTokenKind::LessEqual)) {
                auto rhs = parse_shift();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = (*lhs <= *rhs) ? 1LL : 0LL;
                continue;
            }
            if (consume(ScanPpTokenKind::Greater)) {
                auto rhs = parse_shift();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = (*lhs > *rhs) ? 1LL : 0LL;
                continue;
            }
            if (consume(ScanPpTokenKind::GreaterEqual)) {
                auto rhs = parse_shift();
                if (!rhs.has_value()) {
                    return std::nullopt;
                }
                lhs = (*lhs >= *rhs) ? 1LL : 0LL;
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
            return (*value == 0) ? 1LL : 0LL;
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
            return object_like_macros_.contains(ident) ? 1LL : 0LL;
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
                    return 1LL;
                }
                if (depth_ >= 32) {
                    return std::nullopt;
                }
                ScanPpExpressionParser nested{macro_value, object_like_macros_, depth_ + 1};
                return nested.parse();
            }
            return 0LL;
        }

        return std::nullopt;
    }

    const std::vector<ScanPpToken>                      tokens_;
    const std::unordered_map<std::string, std::string> &object_like_macros_;
    std::size_t                                         index_ = 0;
    std::size_t                                         depth_ = 0;
};

inline bool scan_has_include_literal(std::string_view expr, const std::filesystem::path &source_directory,
                                     const std::vector<std::filesystem::path>           &include_search_paths,
                                     const std::unordered_map<std::string, std::string> &object_like_macros, bool &handled) {
    handled = false;
    expr    = trim_ascii_view(expr);
    if (!expr.starts_with("__has_include")) {
        return false;
    }
    expr.remove_prefix(std::string_view{"__has_include"}.size());
    expr = trim_ascii_view(expr);
    if (expr.empty() || expr.front() != '(' || expr.back() != ')') {
        return false;
    }
    expr.remove_prefix(1);
    expr.remove_suffix(1);
    expr = trim_ascii_view(expr);
    if (expr.size() < 2) {
        return false;
    }

    auto resolve_operand = [&](std::string_view operand, auto &&self) -> std::optional<std::pair<std::filesystem::path, bool>> {
        operand = trim_ascii_view(operand);
        if (operand.size() >= 2 && operand.front() == '"' && operand.back() == '"') {
            return std::pair{std::filesystem::path{std::string(operand.substr(1, operand.size() - 2))}, false};
        }
        if (operand.size() >= 2 && operand.front() == '<' && operand.back() == '>') {
            return std::pair{std::filesystem::path{std::string(operand.substr(1, operand.size() - 2))}, true};
        }
        if (const auto ident = parse_scan_identifier(operand); ident.has_value() && *ident == trim_ascii_copy(operand)) {
            if (const auto it = object_like_macros.find(*ident); it != object_like_macros.end()) {
                return self(trim_ascii_view(it->second), self);
            }
        }
        return std::nullopt;
    };

    const auto resolved_operand = resolve_operand(expr, resolve_operand);
    if (!resolved_operand.has_value()) {
        return false;
    }

    const auto &[include_name, angled] = *resolved_operand;
    handled                            = true;

    std::error_code ec;
    if (!angled && !source_directory.empty()) {
        if (std::filesystem::exists(source_directory / include_name, ec)) {
            return true;
        }
        ec.clear();
    }

    for (const auto &dir : include_search_paths) {
        if (dir.empty()) {
            continue;
        }
        if (std::filesystem::exists(dir / include_name, ec)) {
            return true;
        }
        ec.clear();
    }
    return false;
}

inline std::string rewrite_scan_has_include_operators(std::string_view text, const std::filesystem::path &source_directory,
                                                      const std::vector<std::filesystem::path>           &include_search_paths,
                                                      const std::unordered_map<std::string, std::string> &object_like_macros) {
    std::string out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size();) {
        if (text.substr(i).starts_with("__has_include")) {
            const std::size_t begin = i;
            i += std::string_view{"__has_include"}.size();
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            if (i >= text.size() || text[i] != '(') {
                out.append(text.substr(begin, i - begin));
                continue;
            }

            std::size_t depth = 0;
            std::size_t end   = i;
            for (; end < text.size(); ++end) {
                if (text[end] == '(') {
                    ++depth;
                } else if (text[end] == ')') {
                    --depth;
                    if (depth == 0) {
                        ++end;
                        break;
                    }
                }
            }
            if (end > text.size() || depth != 0) {
                out.append(text.substr(begin));
                break;
            }

            bool       handled = false;
            const bool found   = scan_has_include_literal(text.substr(begin, end - begin), source_directory, include_search_paths,
                                                          object_like_macros, handled);
            if (handled) {
                out.push_back(found ? '1' : '0');
            } else {
                out.append(text.substr(begin, end - begin));
            }
            i = end;
            continue;
        }
        out.push_back(text[i]);
        ++i;
    }

    return out;
}

inline std::optional<bool> evaluate_simple_preprocessor_condition(std::string_view                                    text,
                                                                  const std::unordered_map<std::string, std::string> &object_like_macros,
                                                                  const std::filesystem::path                        &source_directory = {},
                                                                  const std::vector<std::filesystem::path> &include_search_paths = {}) {
    text = trim_ascii_view(text);
    if (text.empty()) {
        return std::nullopt;
    }
    const std::string      rewritten = rewrite_scan_has_include_operators(text, source_directory, include_search_paths, object_like_macros);
    ScanPpExpressionParser parser{rewritten, object_like_macros};
    if (const auto value = parser.parse(); value.has_value()) {
        return *value != 0;
    }
    return std::nullopt;
}

inline void update_preprocessor_branch_state(ScanStreamState &state) {
    state.current_branch_active = state.conditionals.empty() ? true : state.conditionals.back().active;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline void warn_unknown_preprocessor_condition(std::string_view keyword, std::string_view expr, std::size_t line_number,
                                                const ScanStreamState &state) {
    if (!state.warn_on_unknown_conditions || keyword.empty()) {
        return;
    }

    const std::string normalized_expr = trim_ascii_copy(expr);
    if (normalized_expr.empty()) {
        return;
    }

    static std::mutex                      warned_mutex;
    static std::unordered_set<std::string> warned_conditions;

    const std::string source = state.source_path.empty() ? std::string{"<unknown>"} : state.source_path.generic_string();
    const std::string key    = source + ":" + std::to_string(line_number) + ":" + std::string(keyword) + ":" + normalized_expr;

    {
        std::lock_guard<std::mutex> lock(warned_mutex);
        if (!warned_conditions.insert(key).second) {
            return;
        }
    }

    if (state.unknown_condition_warning_sink) {
        state.unknown_condition_warning_sink(source, line_number, keyword, normalized_expr);
        return;
    }

    static std::mutex           stderr_mutex;
    std::lock_guard<std::mutex> lock(stderr_mutex);
    static_cast<void>(std::fprintf(stderr,
                                   "gentest_codegen: warning: unable to evaluate preprocessor condition during module/import scan at "
                                   "%s:%zu; "
                                   "treating #%.*s branch as inactive: %s\n",
                                   source.c_str(), line_number, static_cast<int>(keyword.size()), keyword.data(), normalized_expr.c_str()));
}

inline void handle_preprocessor_logical_line(std::string_view raw_line, ScanStreamState &state, std::size_t line_number) {
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
    const std::string_view rest    = trim_ascii_view(cursor.substr(keyword_end));

    if (keyword == "if") {
        const bool parent_active = state.current_branch_active;
        const auto evaluated =
            evaluate_simple_preprocessor_condition(rest, state.object_like_macros, state.source_directory, state.include_search_paths);
        if (parent_active && !evaluated.has_value()) {
            warn_unknown_preprocessor_condition(keyword, rest, line_number, state);
        }
        const bool branch_active = parent_active && evaluated.value_or(false);
        state.conditionals.push_back(ScanConditionalFrame{
            .parent_active = parent_active,
            .branch_taken  = branch_active,
            .active        = branch_active,
        });
        update_preprocessor_branch_state(state);
        return;
    }

    if (keyword == "ifdef" || keyword == "ifndef") {
        const bool parent_active = state.current_branch_active;
        const auto ident         = parse_scan_identifier(rest);
        const bool defined       = ident.has_value() && state.object_like_macros.contains(*ident);
        const bool branch_active = parent_active && (keyword == "ifdef" ? defined : !defined);
        state.conditionals.push_back(ScanConditionalFrame{
            .parent_active = parent_active,
            .branch_taken  = branch_active,
            .active        = branch_active,
        });
        update_preprocessor_branch_state(state);
        return;
    }

    if (keyword == "elif") {
        if (state.conditionals.empty()) {
            return;
        }
        auto      &frame = state.conditionals.back();
        const auto evaluated =
            evaluate_simple_preprocessor_condition(rest, state.object_like_macros, state.source_directory, state.include_search_paths);
        if (frame.parent_active && !frame.branch_taken && !evaluated.has_value()) {
            warn_unknown_preprocessor_condition(keyword, rest, line_number, state);
        }
        const bool branch_active = frame.parent_active && !frame.branch_taken && evaluated.value_or(false);
        frame.active             = branch_active;
        frame.branch_taken       = frame.branch_taken || branch_active;
        update_preprocessor_branch_state(state);
        return;
    }

    if (keyword == "else") {
        if (state.conditionals.empty()) {
            return;
        }
        auto      &frame         = state.conditionals.back();
        const bool branch_active = frame.parent_active && !frame.branch_taken;
        frame.active             = branch_active;
        frame.branch_taken       = true;
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
    ++state.current_line;
    const std::string      stripped        = strip_comments_for_line_scan(raw_line, state.in_block_comment);
    const std::string_view trimmed         = trim_ascii_view(stripped);
    const bool             is_preprocessor = state.in_preprocessor_continuation || (!trimmed.empty() && trimmed.front() == '#');

    if (is_preprocessor) {
        processed.is_preprocessor = true;
        if (!trimmed.empty()) {
            if (state.pending_preprocessor.empty()) {
                state.pending_preprocessor_start_line = state.current_line;
            }
            if (!state.pending_preprocessor.empty()) {
                state.pending_preprocessor.push_back(' ');
            }
            state.pending_preprocessor.append(trimmed);
        }

        state.in_preprocessor_continuation = has_trailing_line_continuation(trimmed);
        if (state.in_preprocessor_continuation) {
            strip_trailing_line_continuation(state.pending_preprocessor);
        } else if (!state.pending_preprocessor.empty()) {
            handle_preprocessor_logical_line(state.pending_preprocessor, state, state.pending_preprocessor_start_line);
            state.pending_preprocessor.clear();
            state.pending_preprocessor_start_line = 0;
        }
        return processed;
    }

    if (!state.current_branch_active) {
        return processed;
    }

    processed.stripped       = std::string{trimmed};
    processed.is_active_code = !processed.stripped.empty();
    return processed;
}

inline std::vector<std::string> split_scan_statements(std::string_view line) {
    std::vector<std::string> statements;
    std::string              current;
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

inline bool is_preprocessor_directive_scan_line(std::string_view line) { return trim_ascii_copy(line).starts_with('#'); }

inline bool is_global_module_fragment_scan_line(std::string_view line) { return normalize_scan_directive_line(line) == "module;"; }

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

    return canonicalize_scan_module_name(cursor.substr(0, semi), false);
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

    return canonicalize_scan_module_name(cursor.substr(0, semi), true);
}

inline std::string normalize_scan_module_preamble_source(std::string_view text) {
    enum class PendingKind {
        None,
        NamedModule,
        Import,
    };

    auto preamble_requires_rewrite = [&]() -> bool {
        bool            seen_named_module = false;
        PendingKind     pending_kind      = PendingKind::None;
        std::string     pending_statement;
        ScanStreamState scan_state;
        scan_state.warn_on_unknown_conditions = false;
        std::size_t cursor                    = 0;

        while (cursor < text.size()) {
            const std::size_t line_end   = text.find('\n', cursor);
            const std::size_t next       = line_end == std::string_view::npos ? text.size() : line_end + 1;
            std::string_view  line       = text.substr(cursor, next - cursor);
            std::string_view  line_no_nl = line;
            if (!line_no_nl.empty() && line_no_nl.back() == '\n') {
                line_no_nl.remove_suffix(1);
            }
            if (!line_no_nl.empty() && line_no_nl.back() == '\r') {
                line_no_nl.remove_suffix(1);
            }

            const auto processed = process_scan_physical_line(line_no_nl, scan_state);
            if (!processed.is_active_code) {
                cursor = next;
                continue;
            }

            const auto statements = split_scan_statements(processed.stripped);
            if (statements.size() > 1) {
                return true;
            }

            bool line_is_preamble = true;
            for (const auto &statement : statements) {
                if (!seen_named_module) {
                    if (is_global_module_fragment_scan_line(statement)) {
                        continue;
                    }
                    if (pending_kind == PendingKind::NamedModule) {
                        if (!pending_statement.empty()) {
                            pending_statement.push_back(' ');
                        }
                        pending_statement.append(statement);
                        return true;
                    }
                    if (!looks_like_named_module_scan_prefix(statement)) {
                        line_is_preamble = false;
                        break;
                    }
                    pending_statement = statement;
                    pending_kind      = PendingKind::NamedModule;
                    if (statement.find(';') == std::string::npos) {
                        continue;
                    }
                    if (!parse_named_module_name_from_scan_line(pending_statement).has_value()) {
                        line_is_preamble = false;
                        break;
                    }
                    pending_statement.clear();
                    pending_kind      = PendingKind::None;
                    seen_named_module = true;
                    continue;
                }

                if (pending_kind == PendingKind::Import) {
                    if (!pending_statement.empty()) {
                        pending_statement.push_back(' ');
                    }
                    pending_statement.append(statement);
                    return true;
                }
                if (!looks_like_import_scan_prefix(statement)) {
                    line_is_preamble = false;
                    break;
                }
                pending_statement = statement;
                pending_kind      = PendingKind::Import;
                if (statement.find(';') == std::string::npos) {
                    continue;
                }
                if (!is_any_import_scan_line(pending_statement)) {
                    line_is_preamble = false;
                    break;
                }
                pending_statement.clear();
                pending_kind = PendingKind::None;
            }

            if (!line_is_preamble) {
                break;
            }
            cursor = next;
        }

        return false;
    };

    if (!preamble_requires_rewrite()) {
        return std::string{text};
    }

    std::string out;
    out.reserve(text.size());

    bool            seen_named_module = false;
    PendingKind     pending_kind      = PendingKind::None;
    std::string     pending_statement;
    std::size_t     cursor = 0;
    ScanStreamState scan_state;
    scan_state.warn_on_unknown_conditions = false;

    auto flush_named_module = [&]() -> bool {
        const auto module_name = parse_named_module_name_from_scan_line(pending_statement);
        if (!module_name.has_value()) {
            return false;
        }
        const std::string normalized = normalize_scan_directive_line(pending_statement);
        if (normalized.starts_with("export ")) {
            out.append("export module ");
        } else {
            out.append("module ");
        }
        out.append(*module_name);
        out.append(";\n");
        pending_statement.clear();
        pending_kind      = PendingKind::None;
        seen_named_module = true;
        return true;
    };

    auto flush_import = [&]() -> bool {
        if (!is_any_import_scan_line(pending_statement)) {
            return false;
        }
        out.append(normalize_scan_directive_line(pending_statement));
        out.push_back('\n');
        pending_statement.clear();
        pending_kind = PendingKind::None;
        return true;
    };

    while (cursor < text.size()) {
        const std::size_t line_end   = text.find('\n', cursor);
        const std::size_t next       = line_end == std::string_view::npos ? text.size() : line_end + 1;
        std::string_view  line       = text.substr(cursor, next - cursor);
        std::string_view  line_no_nl = line;
        if (!line_no_nl.empty() && line_no_nl.back() == '\n') {
            line_no_nl.remove_suffix(1);
        }
        if (!line_no_nl.empty() && line_no_nl.back() == '\r') {
            line_no_nl.remove_suffix(1);
        }

        const auto processed = process_scan_physical_line(line_no_nl, scan_state);
        if (!processed.is_active_code) {
            out.append(text.substr(cursor, next - cursor));
            cursor = next;
            continue;
        }

        bool line_is_preamble = true;
        for (const auto &statement : split_scan_statements(processed.stripped)) {
            if (!seen_named_module) {
                if (is_global_module_fragment_scan_line(statement)) {
                    out.append("module;\n");
                    continue;
                }
                if (pending_kind == PendingKind::NamedModule) {
                    if (!pending_statement.empty()) {
                        pending_statement.push_back(' ');
                    }
                    pending_statement.append(statement);
                } else {
                    if (!looks_like_named_module_scan_prefix(statement)) {
                        line_is_preamble = false;
                        break;
                    }
                    pending_statement = statement;
                    pending_kind      = PendingKind::NamedModule;
                }
                if (statement.find(';') == std::string::npos) {
                    continue;
                }
                if (!flush_named_module()) {
                    line_is_preamble = false;
                    break;
                }
                continue;
            }

            if (pending_kind == PendingKind::Import) {
                if (!pending_statement.empty()) {
                    pending_statement.push_back(' ');
                }
                pending_statement.append(statement);
            } else {
                if (!looks_like_import_scan_prefix(statement)) {
                    line_is_preamble = false;
                    break;
                }
                pending_statement = statement;
                pending_kind      = PendingKind::Import;
            }
            if (statement.find(';') == std::string::npos) {
                continue;
            }
            if (!flush_import()) {
                line_is_preamble = false;
                break;
            }
        }

        if (!line_is_preamble) {
            out.append(text.substr(cursor));
            return out;
        }
        cursor = next;
    }

    if (pending_kind == PendingKind::NamedModule && flush_named_module()) {
        return out;
    }
    if (pending_kind == PendingKind::Import && flush_import()) {
        return out;
    }
    if (!pending_statement.empty()) {
        out.append(pending_statement);
    }
    return out;
}

struct ScanIncludeDirective {
    std::string header;
    bool        angled = false;
};

inline std::optional<ScanIncludeDirective> parse_include_directive_from_scan_line(std::string_view line) {
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

    const char open  = cursor.front();
    const char close = open == '<' ? '>' : (open == '"' ? '"' : '\0');
    if (close == '\0') {
        return std::nullopt;
    }
    const auto end = cursor.find(close, 1);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return ScanIncludeDirective{
        .header = std::string{cursor.substr(1, end - 1)},
        .angled = open == '<',
    };
}

inline std::optional<std::string> parse_include_header_from_scan_line(std::string_view line) {
    if (const auto include = parse_include_directive_from_scan_line(line); include.has_value()) {
        return include->header;
    }
    return std::nullopt;
}

inline std::optional<std::string> named_module_name_from_source_file(const std::filesystem::path              &path,
                                                                     const std::vector<std::filesystem::path> &include_search_paths = {},
                                                                     std::span<const std::string>              command_line         = {}) {
    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }

    ScanStreamState state;
    state.source_path          = path;
    state.source_directory     = path.parent_path();
    state.include_search_paths = default_scan_include_search_paths(state.source_directory, include_search_paths);
    populate_scan_macros_from_command_line(state, command_line);
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
                pending        = statement;
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
