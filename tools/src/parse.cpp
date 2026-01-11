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

void scan_attributes_before(AttributeCollection &collected, llvm::StringRef buffer, std::size_t start_offset) {
    auto rewind_over_ws_and_comments = [&](std::size_t position) -> std::size_t {
        while (position > 0) {
            while (position > 0 && std::isspace(static_cast<unsigned char>(buffer[position - 1])) != 0) {
                --position;
            }

            // Skip block comments (/* ... */) and line comments (// ...).
            if (position >= 2 && buffer[position - 2] == '*' && buffer[position - 1] == '/') {
                const llvm::StringRef prefix = buffer.take_front(position - 2);
                const std::size_t     open   = prefix.rfind("/*");
                if (open == llvm::StringRef::npos) {
                    break;
                }
                position = open;
                continue;
            }

            if (position > 0) {
                std::size_t line_begin = 0;
                const auto  prev_nl    = buffer.rfind('\n', position - 1);
                if (prev_nl != llvm::StringRef::npos) {
                    line_begin = prev_nl + 1;
                }

                const llvm::StringRef line        = buffer.slice(line_begin, position);
                const std::size_t     comment_pos = line.find("//");
                if (comment_pos != llvm::StringRef::npos) {
                    llvm::StringRef before = line.take_front(comment_pos);
                    while (!before.empty() && std::isspace(static_cast<unsigned char>(before.back())) != 0) {
                        before = before.drop_back();
                    }
                    // Allow comments on an otherwise-empty line and trailing comments after an attribute.
                    if (before.empty() || before.ends_with("]]")) {
                        position = line_begin;
                        continue;
                    }
                }
            }

            break;
        }
        return position;
    };

    std::size_t cursor = start_offset <= static_cast<std::size_t>(buffer.size()) ? start_offset : buffer.size();
    while (cursor > 0) {
        std::size_t position = rewind_over_ws_and_comments(cursor);

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
        std::string_view      view(attribute_text.data(), attribute_text.size());
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
        std::string_view namespace_name(view.substr(0, ns_len));
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
        std::string_view args_text = trim_view(view.substr(0, args_end));

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

    const unsigned loc_offset = sm.getFileOffset(sm.getSpellingLoc(ns.getLocation()));
    scan_attributes_before(collected, buffer, loc_offset);

    return collected;
}

} // namespace gentest::codegen
