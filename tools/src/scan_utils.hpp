#pragma once

#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

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

inline std::string trim_ascii_copy(std::string_view text) {
    return std::string{trim_ascii_view(text)};
}

inline std::string ltrim_ascii_copy(std::string_view text) {
    return std::string{ltrim_ascii_view(text)};
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

    std::string line;
    std::string pending;
    bool        pending_active = false;
    bool        in_block_comment = false;
    while (std::getline(in, line)) {
        std::string trimmed = trim_ascii_copy(strip_comments_for_line_scan(line, in_block_comment));
        if (trimmed.empty()) {
            continue;
        }
        if (is_preprocessor_directive_scan_line(trimmed)) {
            continue;
        }

        if (!pending_active) {
            if (!looks_like_named_module_scan_prefix(trimmed) && !is_global_module_fragment_scan_line(trimmed)) {
                continue;
            }
            pending = trimmed;
            pending_active = true;
        } else {
            pending.push_back(' ');
            pending.append(trimmed);
        }

        if (trimmed.find(';') == std::string::npos) {
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
    return std::nullopt;
}

} // namespace gentest::codegen::scan
