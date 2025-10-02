// Implementation of parsing helpers for gentest attributes

#include "parse.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

using namespace clang;

namespace gentest::codegen {

namespace {

bool is_identifier_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '-';
}

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

} // namespace

auto collect_gentest_attributes_for(const FunctionDecl &func, const SourceManager &sm) -> AttributeCollection {
    AttributeCollection collected;

    SourceLocation begin = func.getBeginLoc();
    if (!begin.isValid()) {
        return collected;
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

    std::size_t cursor = offset;
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

        const std::string attribute_text = buffer.slice(open, close_marker + 2).str();

        std::string_view view(attribute_text);
        if (!view.starts_with("[[")) {
            cursor = open;
            continue;
        }
        view.remove_prefix(2);
        while (!view.empty() && std::isspace(static_cast<unsigned char>(view.front())) != 0) {
            view.remove_prefix(1);
        }
        if (!view.starts_with("using")) {
            cursor = open;
            continue;
        }
        view.remove_prefix(5);
        while (!view.empty() && std::isspace(static_cast<unsigned char>(view.front())) != 0) {
            view.remove_prefix(1);
        }
        std::size_t ns_len = 0;
        while (ns_len < view.size() && is_identifier_char(view[ns_len])) {
            ++ns_len;
        }
        std::string namespace_name(view.substr(0, ns_len));
        view.remove_prefix(ns_len);
        while (!view.empty() && std::isspace(static_cast<unsigned char>(view.front())) != 0) {
            view.remove_prefix(1);
        }
        if (view.empty() || view.front() != ':') {
            cursor = open;
            continue;
        }
        view.remove_prefix(1);

        std::size_t args_end = view.rfind("]]");
        if (args_end == std::string_view::npos) {
            cursor = open;
            continue;
        }
        std::string args_text = trim_copy(view.substr(0, args_end));

        if (namespace_name != "gentest") {
            collected.other_namespaces.push_back(attribute_text);
            cursor = open;
            continue;
        }

        auto parsed = parse_attribute_list(args_text);
        if (!parsed.empty()) {
            collected.gentest.insert(collected.gentest.begin(), parsed.begin(), parsed.end());
        }

        cursor = open;
    }

    return collected;
}

} // namespace gentest::codegen
