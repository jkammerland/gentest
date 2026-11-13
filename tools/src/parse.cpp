// Implementation of parsing helpers for gentest attributes

#include "parse.hpp"

#include <algorithm>
#include <cctype>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/LangStandard.h>
#include <clang/Basic/TokenKinds.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/Token.h>
#include <cstddef>
#include <functional>
#include <llvm/TargetParser/Triple.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
bool should_enter_char_literal(std::string_view text, std::size_t index);

std::string to_lower_copy(std::string_view text) {
    std::string lowered(text);
    for (char &ch : lowered) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return lowered;
}

bool is_known_gentest_attribute_name(std::string_view name) {
    const std::string lowered = to_lower_copy(name);
    return lowered == "test" || lowered == "bench" || lowered == "benchmark" || lowered == "baseline" || lowered == "jitter" ||
           lowered == "req" || lowered == "requires" || lowered == "skip" || lowered == "template" || lowered == "parameters" ||
           lowered == "items_per_call" || lowered == "ops_per_call" || lowered == "bench_once" || lowered == "bench_warmup" ||
           lowered == "bench_time" || lowered == "range" || lowered == "linspace" || lowered == "geom" || lowered == "geomspace" ||
           lowered == "geospace" || lowered == "logspace" || lowered == "parameters_pack" || lowered == "fixtures" || lowered == "fast" ||
           lowered == "slow" || lowered == "linux" || lowered == "windows" || lowered == "death" || lowered == "owner" ||
           lowered == "fixture" || lowered == "suite";
}

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

bool is_known_gentest_unscoped_token(std::string_view token) {
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
    const std::string_view name = token.substr(0, cursor);
    while (cursor < token.size() && std::isspace(static_cast<unsigned char>(token[cursor])) != 0) {
        ++cursor;
    }
    if (cursor + 1 < token.size() && token[cursor] == ':' && token[cursor + 1] == ':') {
        return false;
    }

    return is_known_gentest_attribute_name(name);
}

bool extract_gentest_scoped_payload(std::string_view list, std::string &payload, std::vector<std::string> &mis_scoped_gentest) {
    payload.clear();
    mis_scoped_gentest.clear();

    struct CandidateToken {
        std::string_view token;
    };

    std::vector<CandidateToken> candidates;
    bool                        saw_gentest_scoped = false;

    auto collect_if_gentest_related = [&](std::size_t start, std::size_t end) {
        if (end < start) {
            return;
        }
        const std::string_view token = trim_view(list.substr(start, end - start));
        if (token.empty()) {
            return;
        }
        if (is_gentest_scoped_attribute_token(token)) {
            candidates.push_back(CandidateToken{.token = token});
            saw_gentest_scoped = true;
            return;
        }
        if (is_known_gentest_unscoped_token(token)) {
            mis_scoped_gentest.emplace_back(token);
        }
    };

    auto append_candidate = [&](std::string_view token) {
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
        if (ch == '\'' && should_enter_char_literal(list, i)) {
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
            collect_if_gentest_related(token_start, i);
            token_start = i + 1;
        }
    }

    collect_if_gentest_related(token_start, list.size());
    if (!saw_gentest_scoped) {
        return false;
    }

    for (const auto &candidate : candidates) {
        append_candidate(candidate.token);
    }
    return !payload.empty();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool parse_attribute_namespace_and_payload(std::string_view attribute_text, std::string_view &namespace_name, std::string_view &args_text,
                                           std::string &owned_args_text, std::vector<std::string> &mis_scoped_gentest) {
    mis_scoped_gentest.clear();
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
        if (!extract_gentest_scoped_payload(attribute_list, owned_args_text, mis_scoped_gentest)) {
            return false;
        }
        namespace_name = "gentest";
        args_text      = owned_args_text;
        return true;
    }

    namespace_name = view.substr(0, ns_len);
    view.remove_prefix(ns_len);
    view = trim_view(view);
    if (!view.starts_with("::")) {
        if (!extract_gentest_scoped_payload(attribute_list, owned_args_text, mis_scoped_gentest)) {
            return false;
        }
        namespace_name = "gentest";
        args_text      = owned_args_text;
        return true;
    }

    if (namespace_name != "gentest") {
        if (extract_gentest_scoped_payload(attribute_list, owned_args_text, mis_scoped_gentest)) {
            namespace_name = "gentest";
            args_text      = owned_args_text;
            return true;
        }
        return false;
    }

    // For gentest:: lists, normalize mixed tokens so foreign namespaces (or
    // unrelated standard attributes) are ignored regardless of token order,
    // but bare gentest-like tokens still diagnose as mis-scoped.
    if (extract_gentest_scoped_payload(attribute_list, owned_args_text, mis_scoped_gentest)) {
        namespace_name = "gentest";
        args_text      = owned_args_text;
        return true;
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
    // An apostrophe surrounded by preprocessing-number characters is a C++
    // digit separator, not the beginning of a character literal.
    if (std::isalnum(static_cast<unsigned char>(prev)) != 0 && std::isalnum(static_cast<unsigned char>(next)) != 0) {
        return false;
    }
    return true;
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

struct RawToken {
    std::size_t        begin = 0;
    std::size_t        end   = 0;
    tok::TokenKind     kind  = tok::unknown;
    [[nodiscard]] bool is(tok::TokenKind expected) const { return kind == expected; }
};

struct RawTokenCacheEntry {
    std::size_t           size = 0;
    std::size_t           hash = 0;
    std::vector<RawToken> tokens;
};

std::vector<RawToken> lex_raw_tokens(llvm::StringRef buffer) {
    LangOptions              lang_options;
    std::vector<std::string> includes;
    LangOptions::setLangDefaults(lang_options, Language::CXX, llvm::Triple{}, includes, LangStandard::lang_cxx20);

    // A raw lexer needs no preprocessor: it never diagnoses, and it ends an
    // unterminated string or character literal at the end of its line, so
    // prose inside comments or preprocessor-skipped text cannot swallow the
    // rest of the file. Offsets are recovered from the raw location encoding.
    std::vector<RawToken> tokens;
    Lexer                 lexer(SourceLocation{}, lang_options, buffer.begin(), buffer.begin(), buffer.end());
    Token                 token;
    for (;;) {
        lexer.LexFromRawLexer(token);
        if (token.is(tok::eof)) {
            break;
        }
        const std::size_t begin = token.getLocation().getRawEncoding();
        tokens.push_back(RawToken{.begin = begin, .end = begin + token.getLength(), .kind = token.getKind()});
    }
    return tokens;
}

// Every declaration in a header asks for the tokens of the same buffer, so
// keep them per thread. Entries are verified by content hash because a buffer
// address can be reused by a later translation unit.
const std::vector<RawToken> &raw_tokens_for(llvm::StringRef buffer) {
    static constexpr std::size_t                                      kMaxCachedBuffers = 64;
    thread_local std::unordered_map<const char *, RawTokenCacheEntry> cache;

    const std::size_t hash = std::hash<std::string_view>{}(std::string_view(buffer.data(), buffer.size()));
    if (const auto it = cache.find(buffer.data()); it != cache.end()) {
        if (it->second.size == buffer.size() && it->second.hash == hash) {
            return it->second.tokens;
        }
        cache.erase(it);
    }
    if (cache.size() >= kMaxCachedBuffers) {
        cache.clear();
    }
    auto &entry  = cache[buffer.data()];
    entry.size   = buffer.size();
    entry.hash   = hash;
    entry.tokens = lex_raw_tokens(buffer);
    return entry.tokens;
}

// Step backward over declaration specifiers written between an attribute list
// and the location Clang reports for the declaration: keywords and macros
// (inline, static, virtual, constexpr, const, API_EXPORT, ...) and
// parenthesized specifier arguments such as explicit(...) or alignas(...).
std::size_t skip_decl_specifiers_backward(const std::vector<RawToken> &tokens, std::size_t cursor) {
    while (cursor > 0) {
        const RawToken &previous = tokens[cursor - 1];
        if (previous.is(tok::raw_identifier)) {
            --cursor;
            continue;
        }
        if (!previous.is(tok::r_paren)) {
            break;
        }
        std::size_t depth = 0;
        std::size_t open  = cursor;
        bool        found = false;
        while (open > 0) {
            --open;
            if (tokens[open].is(tok::r_paren)) {
                ++depth;
            } else if (tokens[open].is(tok::l_paren)) {
                --depth;
                if (depth == 0) {
                    found = true;
                    break;
                }
            }
        }
        if (!found || open == 0 || !tokens[open - 1].is(tok::raw_identifier)) {
            break;
        }
        cursor = open - 1;
    }
    return cursor;
}

void scan_attributes_before(AttributeCollection &collected, llvm::StringRef buffer, std::size_t start_offset,
                            bool skip_decl_specifiers = false) {
    const auto &tokens = raw_tokens_for(buffer);

    // tokens[0, cursor) are the tokens that end before the scan position.
    // Comments are not tokens, so block and line comments between attribute
    // lists and the declaration need no special handling.
    std::size_t cursor = static_cast<std::size_t>(
        std::ranges::lower_bound(tokens, start_offset, std::ranges::less{}, [](const RawToken &token) { return token.begin; }) -
        tokens.begin());
    if (skip_decl_specifiers) {
        cursor = skip_decl_specifiers_backward(tokens, cursor);
    }

    while (cursor >= 2) {
        if (!tokens[cursor - 1].is(tok::r_square) || !tokens[cursor - 2].is(tok::r_square)) {
            break;
        }

        // Match the closing "]]" against its opening "[[", allowing nested
        // subscripts inside attribute arguments.
        std::size_t depth = 0;
        std::size_t open  = cursor;
        bool        found = false;
        while (open > 0) {
            --open;
            if (tokens[open].is(tok::r_square)) {
                ++depth;
            } else if (tokens[open].is(tok::l_square)) {
                --depth;
                if (depth == 0) {
                    found = true;
                    break;
                }
            }
        }
        if (!found || !tokens[open + 1].is(tok::l_square)) {
            break;
        }

        const llvm::StringRef    attribute_text = buffer.slice(tokens[open].begin, tokens[cursor - 1].end);
        std::string_view         namespace_name;
        std::string_view         args_text;
        std::string              owned_args_text;
        std::vector<std::string> mis_scoped_gentest;
        if (!parse_attribute_namespace_and_payload(std::string_view(attribute_text.data(), attribute_text.size()), namespace_name,
                                                   args_text, owned_args_text, mis_scoped_gentest)) {
            if (!mis_scoped_gentest.empty()) {
                collected.mis_scoped_gentest.insert(collected.mis_scoped_gentest.end(), mis_scoped_gentest.begin(),
                                                    mis_scoped_gentest.end());
            }
            cursor = open;
            continue;
        }

        if (!mis_scoped_gentest.empty()) {
            collected.mis_scoped_gentest.insert(collected.mis_scoped_gentest.end(), mis_scoped_gentest.begin(), mis_scoped_gentest.end());
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

    // Clang reports the start of a member declaration at its leading
    // attribute list, but the start of a free function after it. Start members
    // at their declared type instead; the token scan then steps back over any
    // specifiers (inline, static, virtual, const, ...) written between the
    // attributes and that location.
    SourceLocation begin;

    if (auto *method = llvm::dyn_cast<CXXMethodDecl>(&func)) {
        if (auto *tsi = method->getTypeSourceInfo()) {
            begin = tsi->getTypeLoc().getBeginLoc();
        }
        if (!begin.isValid()) {
            begin = func.getLocation();
        }
    } else {
        begin = func.getBeginLoc();
    }

    if (!begin.isValid()) {
        return collected;
    }

    // For declaration-producing macros, the attribute tokens live in the
    // macro spelling buffer while the declaration name may come from an
    // invocation argument. Scan backward from the spelled declaration start;
    // the collector separately retains the full expansion chain for identity.
    if (begin.isMacroID()) {
        begin = sm.getSpellingLoc(begin);
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

    scan_attributes_before(collected, buffer, offset, /*skip_decl_specifiers=*/true);

    return collected;
}

auto collect_gentest_attributes_for(const CXXRecordDecl &rec, const SourceManager &sm) -> AttributeCollection {
    AttributeCollection collected;

    SourceLocation rec_loc = rec.getLocation();
    if (!rec_loc.isValid()) {
        return collected;
    }
    if (rec_loc.isMacroID()) {
        rec_loc = sm.getSpellingLoc(rec_loc);
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

    const llvm::StringRef buffer     = sm.getBufferData(file_id);
    const unsigned        loc_offset = sm.getFileOffset(sm.getSpellingLoc(ns.getLocation()));
    scan_attributes_before(collected, buffer, loc_offset);

    return collected;
}

} // namespace gentest::codegen
