// Implementation of parsing helpers for gentest attributes

#include "parse.hpp"

#include <cctype>
#include <string>
#include <string_view>

using namespace clang;

namespace gentest::codegen {

namespace {

bool is_identifier_char(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '-'; }

std::string_view trim_view(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

bool try_start_raw_string(std::string_view text, std::size_t index, std::string &delimiter, std::size_t &open_paren);

bool is_gentest_scoped_attribute_token(std::string_view token) {
    token = trim_view(token);
    if (token.empty()) {
        return false;
    }

    std::size_t cursor = 0;
    if (!std::isalpha(static_cast<unsigned char>(token[cursor])) && token[cursor] != '_') {
        return false;
    }
    ++cursor;
    while (cursor < token.size() && is_identifier_char(token[cursor])) {
        ++cursor;
    }
    const std::string_view namespace_name = token.substr(0, cursor);
    while (cursor < token.size() && std::isspace(static_cast<unsigned char>(token[cursor])) != 0) {
        ++cursor;
    }

    return namespace_name == "gentest" && cursor + 1 < token.size() && token[cursor] == ':' && token[cursor + 1] == ':';
}

bool extract_gentest_scoped_payload(std::string_view list, std::string &payload) {
    payload.clear();

    auto append_if_gentest = [&](std::size_t start, std::size_t end) {
        if (end < start) {
            return;
        }
        const std::string_view token = trim_view(list.substr(start, end - start));
        if (token.empty() || !is_gentest_scoped_attribute_token(token)) {
            return;
        }
        if (!payload.empty()) {
            payload += ", ";
        }
        payload.append(token.data(), token.size());
    };

    bool        in_string        = false;
    bool        in_char          = false;
    bool        in_line_comment  = false;
    bool        in_block_comment = false;
    bool        in_raw_string    = false;
    bool        escape           = false;
    int         nesting_depth    = 0;
    std::string raw_delimiter;

    std::size_t token_start = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const char ch   = list[i];
        const char next = (i + 1 < list.size()) ? list[i + 1] : '\0';

        if (in_line_comment) {
            if (ch == '\n' || ch == '\r') {
                in_line_comment = false;
            }
            continue;
        }
        if (in_block_comment) {
            if (ch == '*' && next == '/') {
                in_block_comment = false;
                ++i;
            }
            continue;
        }
        if (in_raw_string) {
            if (ch == ')') {
                const std::size_t quote_index = i + raw_delimiter.size() + 1;
                if (quote_index < list.size()) {
                    bool delimiter_matches = true;
                    for (std::size_t j = 0; j < raw_delimiter.size(); ++j) {
                        if (list[i + 1 + j] != raw_delimiter[j]) {
                            delimiter_matches = false;
                            break;
                        }
                    }
                    if (delimiter_matches && list[quote_index] == '"') {
                        in_raw_string = false;
                        i             = quote_index;
                    }
                }
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

        if (ch == '/' && next == '/') {
            in_line_comment = true;
            ++i;
            continue;
        }
        if (ch == '/' && next == '*') {
            in_block_comment = true;
            ++i;
            continue;
        }
        std::size_t raw_open_paren = 0;
        if (try_start_raw_string(list, i, raw_delimiter, raw_open_paren)) {
            in_raw_string = true;
            i             = raw_open_paren;
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '\'') {
            in_char = true;
            continue;
        }

        if (ch == '(' || ch == '[' || ch == '{') {
            ++nesting_depth;
            continue;
        }
        if (ch == ')' || ch == ']' || ch == '}') {
            if (nesting_depth > 0) {
                --nesting_depth;
            }
            continue;
        }
        if (ch == ',' && nesting_depth == 0) {
            append_if_gentest(token_start, i);
            token_start = i + 1;
        }
    }

    append_if_gentest(token_start, list.size());
    return !payload.empty();
}

bool parse_attribute_namespace_and_payload(std::string_view attribute_text, std::string_view &namespace_name, std::string_view &args_text,
                                           std::string &owned_args_text) {
    std::string_view view = trim_view(attribute_text);
    if (!view.starts_with("[[") || !view.ends_with("]]")) {
        return false;
    }
    view.remove_prefix(2);
    view.remove_suffix(2);
    view                                  = trim_view(view);
    const std::string_view attribute_list = view;

    if (view.starts_with("using")) {
        view.remove_prefix(5);
        view = trim_view(view);

        std::size_t ns_len = 0;
        while (ns_len < view.size() && is_identifier_char(view[ns_len])) {
            ++ns_len;
        }
        if (ns_len == 0) {
            return false;
        }

        namespace_name = view.substr(0, ns_len);
        view.remove_prefix(ns_len);
        view = trim_view(view);
        if (view.empty() || view.front() != ':') {
            return false;
        }

        view.remove_prefix(1);
        args_text = trim_view(view);
        return true;
    }

    std::size_t ns_len = 0;
    while (ns_len < view.size() && is_identifier_char(view[ns_len])) {
        ++ns_len;
    }
    if (ns_len == 0) {
        if (!extract_gentest_scoped_payload(attribute_list, owned_args_text)) {
            return false;
        }
        namespace_name = "gentest";
        args_text      = owned_args_text;
        return true;
    }

    namespace_name = view.substr(0, ns_len);
    view.remove_prefix(ns_len);
    view = trim_view(view);
    if (view.size() < 2 || view.substr(0, 2) != "::") {
        if (!extract_gentest_scoped_payload(attribute_list, owned_args_text)) {
            return false;
        }
        namespace_name = "gentest";
        args_text      = owned_args_text;
        return true;
    }

    if (namespace_name != "gentest") {
        if (extract_gentest_scoped_payload(attribute_list, owned_args_text)) {
            namespace_name = "gentest";
            args_text      = owned_args_text;
            return true;
        }
    }

    view.remove_prefix(2);
    args_text = trim_view(view);
    return true;
}

bool is_raw_string_delimiter_char(char ch) {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
        return false;
    }
    return ch != '(' && ch != ')' && ch != '\\' && ch != '"';
}

bool try_start_raw_string(std::string_view text, std::size_t index, std::string &delimiter, std::size_t &open_paren) {
    if (index + 2 >= text.size() || text[index] != 'R' || text[index + 1] != '"') {
        return false;
    }

    std::size_t cursor = index + 2;
    while (cursor < text.size() && text[cursor] != '(') {
        if (!is_raw_string_delimiter_char(text[cursor])) {
            return false;
        }
        ++cursor;
    }
    if (cursor >= text.size()) {
        return false;
    }

    delimiter.assign(text.substr(index + 2, cursor - (index + 2)));
    open_paren = cursor;
    return true;
}

std::size_t find_attribute_open_for_close(llvm::StringRef buffer, std::size_t close_marker) {
    if (close_marker + 1 >= static_cast<std::size_t>(buffer.size())) {
        return llvm::StringRef::npos;
    }

    const std::string_view text(buffer.data(), buffer.size());

    bool        in_string        = false;
    bool        in_char          = false;
    bool        in_line_comment  = false;
    bool        in_block_comment = false;
    bool        in_raw_string    = false;
    bool        escape           = false;
    std::string raw_delimiter;

    std::size_t open_marker = llvm::StringRef::npos;
    for (std::size_t i = 0; i <= close_marker && i < text.size(); ++i) {
        const char ch   = text[i];
        const char next = (i + 1 < text.size()) ? text[i + 1] : '\0';

        if (in_line_comment) {
            if (ch == '\n' || ch == '\r') {
                in_line_comment = false;
            }
            continue;
        }
        if (in_block_comment) {
            if (ch == '*' && next == '/') {
                in_block_comment = false;
                ++i;
            }
            continue;
        }
        if (in_raw_string) {
            if (ch == ')') {
                const std::size_t quote_index = i + raw_delimiter.size() + 1;
                if (quote_index < text.size()) {
                    bool delimiter_matches = true;
                    for (std::size_t j = 0; j < raw_delimiter.size(); ++j) {
                        if (text[i + 1 + j] != raw_delimiter[j]) {
                            delimiter_matches = false;
                            break;
                        }
                    }
                    if (delimiter_matches && text[quote_index] == '"') {
                        in_raw_string = false;
                        i             = quote_index;
                    }
                }
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

        if (ch == '/' && next == '/') {
            in_line_comment = true;
            ++i;
            continue;
        }
        if (ch == '/' && next == '*') {
            in_block_comment = true;
            ++i;
            continue;
        }
        std::size_t raw_open_paren = 0;
        if (try_start_raw_string(text, i, raw_delimiter, raw_open_paren)) {
            in_raw_string = true;
            i             = raw_open_paren;
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '\'') {
            in_char = true;
            continue;
        }
        if (ch == '[' && next == '[') {
            open_marker = i;
            ++i;
            continue;
        }
        if (ch == ']' && next == ']') {
            if (i == close_marker) {
                return open_marker;
            }
            open_marker = llvm::StringRef::npos;
            ++i;
        }
    }

    return llvm::StringRef::npos;
}

void scan_attributes_before(AttributeCollection &collected, llvm::StringRef buffer, std::size_t start_offset) {
    auto find_line_comment = [](std::string_view line) -> std::size_t {
        bool        in_string     = false;
        bool        in_char       = false;
        bool        in_raw_string = false;
        bool        escape        = false;
        std::string raw_delimiter;
        for (std::size_t i = 0; i < line.size(); ++i) {
            const char ch   = line[i];
            const char next = (i + 1 < line.size()) ? line[i + 1] : '\0';

            if (in_raw_string) {
                if (ch == ')') {
                    const std::size_t quote_index = i + raw_delimiter.size() + 1;
                    if (quote_index < line.size()) {
                        bool delimiter_matches = true;
                        for (std::size_t j = 0; j < raw_delimiter.size(); ++j) {
                            if (line[i + 1 + j] != raw_delimiter[j]) {
                                delimiter_matches = false;
                                break;
                            }
                        }
                        if (delimiter_matches && line[quote_index] == '"') {
                            in_raw_string = false;
                            i             = quote_index;
                        }
                    }
                }
                continue;
            }
            if (escape) {
                escape = false;
                continue;
            }
            if (in_string) {
                if (ch == '\\') {
                    escape = true;
                } else if (ch == '"') {
                    in_string = false;
                }
                continue;
            }
            if (in_char) {
                if (ch == '\\') {
                    escape = true;
                } else if (ch == '\'') {
                    in_char = false;
                }
                continue;
            }
            std::size_t raw_open_paren = 0;
            if (try_start_raw_string(line, i, raw_delimiter, raw_open_paren)) {
                in_raw_string = true;
                i             = raw_open_paren;
                continue;
            }
            if (ch == '"') {
                in_string = true;
                continue;
            }
            if (ch == '\'') {
                in_char = true;
                continue;
            }
            if (ch == '/' && next == '/') {
                return i;
            }
        }
        return std::string_view::npos;
    };

    auto skip_ws_and_comments = [&](std::size_t &cursor) {
        bool progressed = true;
        while (progressed && cursor > 0) {
            progressed = false;
            while (cursor > 0 && std::isspace(static_cast<unsigned char>(buffer[cursor - 1])) != 0) {
                --cursor;
                progressed = true;
            }
            if (cursor < 2) {
                return;
            }

            // Skip trailing block comments.
            if (buffer[cursor - 1] == '/' && buffer[cursor - 2] == '*') {
                cursor -= 2;
                while (cursor >= 2) {
                    if (buffer[cursor - 2] == '/' && buffer[cursor - 1] == '*') {
                        cursor -= 2;
                        break;
                    }
                    --cursor;
                }
                progressed = true;
                continue;
            }

            // Skip trailing line comments on the current line.
            std::size_t line_start = cursor;
            while (line_start > 0 && buffer[line_start - 1] != '\n' && buffer[line_start - 1] != '\r') {
                --line_start;
            }
            const llvm::StringRef line = buffer.slice(line_start, cursor);
            const auto            pos  = find_line_comment(std::string_view(line.data(), line.size()));
            if (pos != std::string_view::npos) {
                cursor     = line_start + pos;
                progressed = true;
                continue;
            }
        }
    };

    std::size_t cursor = start_offset <= static_cast<std::size_t>(buffer.size()) ? start_offset : buffer.size();
    while (cursor > 0) {
        std::size_t position = cursor;
        skip_ws_and_comments(position);

        if (position < 2 || buffer[position - 1] != ']' || buffer[position - 2] != ']') {
            break;
        }

        const std::size_t close_marker = position - 2;
        const std::size_t open         = find_attribute_open_for_close(buffer, close_marker);
        if (open == llvm::StringRef::npos) {
            break;
        }

        const llvm::StringRef attribute_text = buffer.slice(open, close_marker + 2);
        std::string_view      namespace_name;
        std::string_view      args_text;
        std::string           owned_args_text;
        if (!parse_attribute_namespace_and_payload(std::string_view(attribute_text.data(), attribute_text.size()), namespace_name,
                                                   args_text, owned_args_text)) {
            cursor = open;
            continue;
        }

        if (namespace_name != "gentest") {
            collected.other_namespaces.push_back(attribute_text.str());
            cursor = open;
            continue;
        }

        auto parsed = parse_attribute_list(args_text);
        if (!parsed.empty()) {
            collected.gentest.insert(collected.gentest.begin(), parsed.begin(), parsed.end());
        }

        cursor = open;
    }
}

} // namespace

auto collect_gentest_attributes_for(const FunctionDecl &func, const SourceManager &sm) -> AttributeCollection {
    AttributeCollection collected;

    // LLVM 21 behavior change: For inline CXX member functions, getBeginLoc() points
    // to the enclosing class, not the function. We need to find the actual start
    // of the function declaration (including any attributes before it).
    //
    // Strategy: Use getSourceRange().getBegin() but adjust for member functions:
    // In LLVM 21, for CXXMethodDecl with inline definitions, all location APIs
    // (getBeginLoc, getOuterLocStart, etc.) incorrectly point to the class.
    // Only getLocation() correctly points to the function name.
    //
    // For member functions: Use getTypeSourceInfo() to find the return type location,
    // which should be before the function name but after any attributes.
    // Then scan backward from there to find attributes.
    SourceLocation begin;

    if (auto *method = llvm::dyn_cast<CXXMethodDecl>(&func)) {
        // For member functions: try to get return type location
        if (auto *tsi = method->getTypeSourceInfo()) {
            begin = tsi->getTypeLoc().getBeginLoc();
        }
        if (!begin.isValid()) {
            // Fallback to function name location for members
            begin = func.getLocation();
        }
    } else {
        // For free functions: getBeginLoc should work correctly
        begin = func.getBeginLoc();
    }

    if (!begin.isValid()) {
        return collected;
    }

    // Expand macros to file location
    if (begin.isMacroID()) {
        begin = sm.getExpansionLoc(begin);
    }

    SourceLocation file_location = sm.getFileLoc(begin);
    if (!file_location.isValid()) {
        return collected;
    }

    const FileID file_id = sm.getFileID(file_location);
    if (file_id.isInvalid()) {
        return collected;
    }

    const llvm::StringRef buffer = sm.getBufferData(file_id);
    const unsigned        offset = sm.getFileOffset(file_location);

    scan_attributes_before(collected, buffer, offset);

    return collected;
}

auto collect_gentest_attributes_for(const CXXRecordDecl &rec, const SourceManager &sm) -> AttributeCollection {
    AttributeCollection collected;

    SourceLocation rec_loc = rec.getLocation();
    if (!rec_loc.isValid()) {
        return collected;
    }
    if (rec_loc.isMacroID()) {
        rec_loc = sm.getExpansionLoc(rec_loc);
    }

    const FileID file_id = sm.getFileID(rec_loc);
    if (file_id.isInvalid()) {
        return collected;
    }

    const llvm::StringRef buffer = sm.getBufferData(file_id);

    const unsigned name_offset = sm.getFileOffset(sm.getSpellingLoc(rec.getLocation()));
    scan_attributes_before(collected, buffer, name_offset);
    if (auto brace = rec.getBraceRange(); brace.isValid()) {
        const auto brace_loc = sm.getSpellingLoc(brace.getBegin());
        if (brace_loc.isValid() && sm.getFileID(brace_loc) == file_id) {
            scan_attributes_before(collected, buffer, sm.getFileOffset(brace_loc));
        }
    }

    return collected;
}

auto collect_gentest_attributes_for(const NamespaceDecl &ns, const SourceManager &sm) -> AttributeCollection {
    AttributeCollection collected;

    SourceLocation ns_loc = ns.getLocation();
    if (!ns_loc.isValid()) {
        return collected;
    }
    if (ns_loc.isMacroID()) {
        ns_loc = sm.getExpansionLoc(ns_loc);
    }

    const FileID file_id = sm.getFileID(ns_loc);
    if (file_id.isInvalid()) {
        return collected;
    }

    const llvm::StringRef buffer = sm.getBufferData(file_id);

    auto scan_back_from = [&](unsigned start_offset) {
        std::size_t cursor = start_offset;
        while (cursor > 0) {
            std::size_t position = cursor;
            while (position > 0 && std::isspace(static_cast<unsigned char>(buffer[position - 1])) != 0) {
                --position;
            }
            if (position < 2 || buffer[position - 1] != ']' || buffer[position - 2] != ']') {
                break;
            }
            const llvm::StringRef prefix = buffer.take_front(position);
            const std::size_t     open   = prefix.rfind("[[");
            if (open == llvm::StringRef::npos) {
                break;
            }
            const std::size_t close_marker = buffer.find("]]", open);
            if (close_marker == llvm::StringRef::npos) {
                break;
            }
            const llvm::StringRef attribute_text = buffer.slice(open, close_marker + 2);
            std::string_view      namespace_name;
            std::string_view      args_text;
            std::string           owned_args_text;
            if (!parse_attribute_namespace_and_payload(std::string_view(attribute_text.data(), attribute_text.size()), namespace_name,
                                                       args_text, owned_args_text)) {
                cursor = static_cast<unsigned>(open);
                continue;
            }

            if (namespace_name != "gentest") {
                collected.other_namespaces.push_back(attribute_text.str());
                cursor = static_cast<unsigned>(open);
                continue;
            }

            auto parsed = parse_attribute_list(args_text);
            if (!parsed.empty()) {
                collected.gentest.insert(collected.gentest.begin(), parsed.begin(), parsed.end());
            }

            cursor = static_cast<unsigned>(open);
        }
    };

    const unsigned loc_offset = sm.getFileOffset(sm.getSpellingLoc(ns.getLocation()));
    scan_back_from(loc_offset);

    return collected;
}

} // namespace gentest::codegen
