#include <algorithm>
#include <array>
#include <cctype>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/Expr.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef GENTEST_TEMPLATE_DIR
#define GENTEST_TEMPLATE_DIR ""
#endif

namespace {
using namespace clang;
using namespace clang::tooling;
using namespace clang::ast_matchers;

constexpr std::string_view kTemplateDir = GENTEST_TEMPLATE_DIR;

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

struct ParsedAttribute {
    std::string              name;
    std::vector<std::string> arguments;
};

struct AttributeCollection {
    std::vector<ParsedAttribute> gentest;
    std::vector<std::string>     other_namespaces;
};

constexpr std::array<std::string_view, 2> kAllowedValueAttributes{"category", "owner"};
constexpr std::array<std::string_view, 4> kAllowedFlagAttributes{"fast", "slow", "linux", "windows"};

bool is_allowed_value_attribute(std::string_view name) {
    return std::find(kAllowedValueAttributes.begin(), kAllowedValueAttributes.end(), name) != kAllowedValueAttributes.end();
}

bool is_allowed_flag_attribute(std::string_view name) {
    return std::find(kAllowedFlagAttributes.begin(), kAllowedFlagAttributes.end(), name) != kAllowedFlagAttributes.end();
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

std::vector<ParsedAttribute> parse_attribute_list(std::string_view list) {
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

        attributes.push_back(ParsedAttribute{std::move(name), std::move(args)});

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

std::vector<ParsedAttribute> parse_gentest_attributes_from_text(std::string_view source) {
    std::vector<ParsedAttribute> collected;
    std::size_t                  search_position = 0;

    while (search_position < source.size()) {
        const std::size_t open = source.find("[[", search_position);
        if (open == std::string_view::npos) {
            break;
        }
        std::size_t cursor = open + 2;

        while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
            ++cursor;
        }
        if (cursor + 5 >= source.size() || source.compare(cursor, 5, "using") != 0) {
            search_position = cursor;
            continue;
        }
        cursor += 5;

        while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
            ++cursor;
        }

        const std::size_t ns_begin = cursor;
        while (cursor < source.size() && is_identifier_char(source[cursor])) {
            ++cursor;
        }
        if (ns_begin == cursor) {
            search_position = cursor;
            continue;
        }
        const std::string namespace_name(source.substr(ns_begin, cursor - ns_begin));
        if (namespace_name != "gentest") {
            search_position = cursor;
            continue;
        }

        while (cursor < source.size() && std::isspace(static_cast<unsigned char>(source[cursor])) != 0) {
            ++cursor;
        }
        if (cursor >= source.size() || source[cursor] != ':') {
            search_position = cursor;
            continue;
        }
        ++cursor;

        const std::size_t list_begin = cursor;
        const std::size_t close      = source.find("]]", cursor);
        if (close == std::string_view::npos) {
            break;
        }

        auto attributes = parse_attribute_list(source.substr(list_begin, close - list_begin));
        collected.insert(collected.end(), std::make_move_iterator(attributes.begin()), std::make_move_iterator(attributes.end()));

        search_position = close + 2;
    }

    return collected;
}

AttributeCollection collect_gentest_attributes_for(const FunctionDecl &func, const SourceManager &sm) {
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

std::vector<int> parse_version_components(std::string_view text) {
    std::vector<int> components;
    std::size_t      pos = 0;
    while (pos < text.size()) {
        if (!std::isdigit(static_cast<unsigned char>(text[pos]))) {
            return {};
        }
        std::size_t end = pos;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])) != 0) {
            ++end;
        }
        components.push_back(std::stoi(std::string(text.substr(pos, end - pos))));
        if (end >= text.size()) {
            break;
        }
        if (text[end] != '.') {
            return {};
        }
        pos = end + 1;
    }
    return components;
}

bool version_less(const std::vector<int> &lhs, const std::vector<int> &rhs) {
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

std::vector<std::string> detect_platform_include_dirs() {
    std::vector<std::string> dirs;
#if defined(__linux__)
    namespace fs = std::filesystem;

    auto append_unique = [&dirs](const fs::path &candidate) {
        if (!fs::exists(candidate) || !fs::is_directory(candidate)) {
            return;
        }
        auto normalized = candidate.lexically_normal().string();
        if (std::find(dirs.begin(), dirs.end(), normalized) == dirs.end()) {
            dirs.push_back(std::move(normalized));
        }
    };

    auto detect_latest_version = [](const fs::path &root) -> std::optional<fs::path> {
        if (!fs::exists(root) || !fs::is_directory(root)) {
            return std::nullopt;
        }
        std::optional<fs::path> best;
        std::vector<int>        best_version;
        for (const auto &entry : fs::directory_iterator(root)) {
            if (!entry.is_directory()) {
                continue;
            }
            auto version = parse_version_components(entry.path().filename().string());
            if (version.empty()) {
                continue;
            }
            if (!best.has_value() || version_less(best_version, version)) {
                best         = entry.path();
                best_version = std::move(version);
            }
        }
        return best;
    };

    if (auto cxx_root = detect_latest_version("/usr/include/c++")) {
        append_unique(*cxx_root);

        fs::path architecture_dir;
        for (const auto &entry : fs::directory_iterator(*cxx_root)) {
            if (!entry.is_directory()) {
                continue;
            }
            const auto name = entry.path().filename().string();
            if (name == "backward") {
                continue;
            }
            if (!name.empty() && name.find('-') != std::string::npos) {
                architecture_dir = entry.path();
                break;
            }
            if (architecture_dir.empty()) {
                architecture_dir = entry.path();
            }
        }
        if (!architecture_dir.empty()) {
            append_unique(architecture_dir);
        }
        append_unique(*cxx_root / "backward");
    }

    auto detect_gcc_internal = [&](const fs::path &root) {
        if (!fs::exists(root) || !fs::is_directory(root)) {
            return std::optional<fs::path>{};
        }

        std::optional<fs::path> best;
        std::vector<int>        best_version;

        for (const auto &triple_dir : fs::directory_iterator(root)) {
            if (!triple_dir.is_directory()) {
                continue;
            }
            for (const auto &version_dir : fs::directory_iterator(triple_dir.path())) {
                if (!version_dir.is_directory()) {
                    continue;
                }
                auto version = parse_version_components(version_dir.path().filename().string());
                if (version.empty()) {
                    continue;
                }
                if (!best.has_value() || version_less(best_version, version)) {
                    best         = version_dir.path();
                    best_version = std::move(version);
                }
            }
        }

        if (best) {
            fs::path include_dir = *best / "include";
            if (fs::exists(include_dir)) {
                return std::optional<fs::path>{include_dir};
            }
        }
        return std::optional<fs::path>{};
    };

    if (auto internal = detect_gcc_internal("/usr/lib/gcc")) {
        append_unique(*internal);
    }
    if (auto internal = detect_gcc_internal("/usr/lib64/gcc")) {
        append_unique(*internal);
    }

    append_unique(fs::path("/usr/include"));
#endif
    return dirs;
}

bool contains_isystem_entry(const clang::tooling::CommandLineArguments &args, const std::string &dir) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "-isystem" && args[i + 1] == dir) {
            return true;
        }
    }
    return false;
}

} // namespace

struct CollectorOptions {
    std::string                          entry = "gentest::run_all_tests";
    std::filesystem::path                output_path;
    std::filesystem::path                template_path;
    std::vector<std::string>             sources;
    std::vector<std::string>             clang_args;
    std::optional<std::filesystem::path> compilation_database;
};

struct TestCaseInfo {
    std::string              qualified_name;
    std::string              display_name;
    std::string              filename;
    unsigned                 line = 0;
    std::vector<std::string> tags;
    std::vector<std::string> requirements;
    bool                     should_skip = false;
    std::string              skip_reason;
};

class TestCaseCollector : public MatchFinder::MatchCallback {
  public:
    explicit TestCaseCollector(std::vector<TestCaseInfo> &out) : out_(out) {}

    void run(const MatchFinder::MatchResult &result) override {
        const auto *func = result.Nodes.getNodeAs<FunctionDecl>("gentest.func");
        if (func == nullptr) {
            return;
        }

        const auto *sm   = result.SourceManager;
        const auto &lang = result.Context->getLangOpts();

        if (func->isTemplated() || func->isDependentContext()) {
            return;
        }

        auto loc = func->getBeginLoc();
        if (loc.isInvalid()) {
            return;
        }
        if (loc.isMacroID()) {
            loc = sm->getExpansionLoc(loc);
        }

        if (!sm->isWrittenInMainFile(loc)) {
            return;
        }

        if (sm->isInSystemHeader(loc) || sm->isWrittenInBuiltinFile(loc)) {
            return;
        }

        std::optional<TestCaseInfo> info = classify(*func, *sm, lang);
        if (!info.has_value()) {
            return;
        }

        // Deduplicate based on qualified name + location
        auto key = std::make_pair(info->qualified_name, std::make_pair(info->filename, info->line));
        if (!seen_.insert(std::move(key)).second) {
            return;
        }

        out_.push_back(std::move(info.value()));
    }

  private:
    struct AttributeSummary {
        std::optional<std::string> case_name;
        std::vector<std::string>   tags;
        std::vector<std::string>   requirements;
        bool                       should_skip = false;
        std::string                skip_reason;
    };

    static void add_unique(std::vector<std::string> &values, std::string value) {
        if (std::find(values.begin(), values.end(), value) == values.end()) {
            values.push_back(std::move(value));
        }
    }

    AttributeSummary extract_metadata(const FunctionDecl &func, const SourceManager &sm, const LangOptions &lang) const {
        AttributeSummary summary;
        (void)lang;

        const auto  collected = collect_gentest_attributes_for(func, sm);
        const auto &parsed    = collected.gentest;

        auto report = [&](std::string_view message) {
            const SourceLocation  loc     = sm.getSpellingLoc(func.getBeginLoc());
            const llvm::StringRef file    = sm.getFilename(loc);
            const unsigned        line    = sm.getSpellingLineNumber(loc);
            const std::string     subject = func.getQualifiedNameAsString();
            if (!file.empty()) {
                llvm::errs() << "gentest_codegen: " << file << ':' << line << ": " << message;
            } else {
                llvm::errs() << "gentest_codegen: " << message;
            }
            if (!subject.empty()) {
                llvm::errs() << " (" << subject << ')';
            }
            llvm::errs() << '\n';
        };

        for (const auto &message : collected.other_namespaces) {
            std::string text = "attribute '" + message + "' ignored (unsupported attribute namespace)";
            report(text);
        }

        if (parsed.empty()) {
            return summary;
        }

        for (const auto &attr : parsed) {
            std::string lowered = attr.name;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

            if (lowered == "test") {
                if (!attr.arguments.empty()) {
                    summary.case_name = attr.arguments.front();
                }
                for (std::size_t idx = 1; idx < attr.arguments.size(); ++idx) {
                    add_unique(summary.tags, "test=" + attr.arguments[idx]);
                }
            } else if (lowered == "req" || lowered == "requires") {
                if (attr.arguments.empty()) {
                    add_unique(summary.requirements, attr.name);
                } else {
                    for (const auto &req : attr.arguments) {
                        add_unique(summary.requirements, req);
                    }
                }
            } else if (lowered == "skip") {
                summary.should_skip = true;
                if (!attr.arguments.empty()) {
                    std::string reason = attr.arguments.front();
                    for (std::size_t idx = 1; idx < attr.arguments.size(); ++idx) {
                        reason.append(", ");
                        reason.append(attr.arguments[idx]);
                    }
                    summary.skip_reason = std::move(reason);
                }
            } else if (attr.arguments.empty()) {
                if (!is_allowed_flag_attribute(lowered)) {
                    had_error_ = true;
                    std::ostringstream stream;
                    stream << "unknown gentest attribute '" << attr.name << "'";
                    report(stream.str());
                    continue;
                }
                add_unique(summary.tags, attr.name);
            } else {
                if (!is_allowed_value_attribute(lowered)) {
                    had_error_ = true;
                    std::ostringstream stream;
                    stream << "unknown gentest attribute '" << attr.name << "' with argument" << (attr.arguments.size() == 1 ? "" : "s")
                           << " (";
                    for (std::size_t idx = 0; idx < attr.arguments.size(); ++idx) {
                        if (idx != 0) {
                            stream << ", ";
                        }
                        stream << '"' << attr.arguments[idx] << '"';
                    }
                    stream << ')';
                    report(stream.str());
                    continue;
                }
                for (const auto &value : attr.arguments) {
                    add_unique(summary.tags, attr.name + "=" + value);
                }
            }
        }

        if (!summary.case_name.has_value()) {
            report("expected [[using gentest : test(\"...\")]] attribute on this test function");
            had_error_ = true;
        }

        return summary;
    }

    std::optional<TestCaseInfo> classify(const FunctionDecl &func, const SourceManager &sm, const LangOptions &lang) const {
        AttributeSummary metadata = extract_metadata(func, sm, lang);
        if (!metadata.case_name.has_value()) {
            return std::nullopt;
        }

        if (!func.doesThisDeclarationHaveABody()) {
            return std::nullopt;
        }

        std::string qualified = func.getQualifiedNameAsString();
        if (qualified.empty()) {
            qualified = func.getNameAsString();
        }
        if (qualified.find("(anonymous namespace)") != std::string::npos) {
            llvm::errs() << "gentest_codegen: ignoring test in anonymous namespace: " << qualified << "\n";
            return std::nullopt;
        }

        auto file_loc = sm.getFileLoc(func.getLocation());
        auto filename = sm.getFilename(file_loc);
        if (filename.empty()) {
            return std::nullopt;
        }

        unsigned line = sm.getSpellingLineNumber(file_loc);

        TestCaseInfo info{};
        info.qualified_name = std::move(qualified);
        info.display_name   = std::move(*metadata.case_name);
        info.filename       = filename.str();
        info.line           = line;
        info.tags           = std::move(metadata.tags);
        info.requirements   = std::move(metadata.requirements);
        info.should_skip    = metadata.should_skip;
        info.skip_reason    = std::move(metadata.skip_reason);
        return info;
    }

    std::vector<TestCaseInfo>                                         &out_;
    std::set<std::pair<std::string, std::pair<std::string, unsigned>>> seen_;
    mutable bool                                                       had_error_ = false;

  public:
    [[nodiscard]] bool has_errors() const { return had_error_; }
};

std::string read_template_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void replace_all(std::string &inout, std::string_view needle, std::string_view replacement) {
    const std::string target{needle};
    const std::string substitute{replacement};
    std::size_t       pos = 0;
    while ((pos = inout.find(target, pos)) != std::string::npos) {
        inout.replace(pos, target.size(), substitute);
        pos += substitute.size();
    }
}

std::string escape_string(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '\"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

std::optional<std::string> render_cases(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases) {
    const auto template_content = read_template_file(options.template_path);
    if (template_content.empty()) {
        llvm::errs() << "gentest_codegen: failed to load template file '" << options.template_path.string() << "'\n";
        return std::nullopt;
    }

    std::map<std::string, std::set<std::string>> forward_decls;
    for (const auto &test : cases) {
        std::string scope;
        std::string basename = test.qualified_name;
        if (auto pos = basename.rfind("::"); pos != std::string::npos) {
            scope    = basename.substr(0, pos);
            basename = basename.substr(pos + 2);
        }
        forward_decls[scope].insert(basename);
    }

    std::string forward_decl_block;
    for (const auto &[scope, functions] : forward_decls) {
        if (scope.empty()) {
            for (const auto &name : functions) {
                forward_decl_block.append("extern void ");
                forward_decl_block.append(name);
                forward_decl_block.append("();\n");
            }
        } else {
            forward_decl_block.append("namespace ");
            forward_decl_block.append(scope);
            forward_decl_block.append(" {\n");
            for (const auto &name : functions) {
                forward_decl_block.append("extern void ");
                forward_decl_block.append(name);
                forward_decl_block.append("();\n");
            }
            forward_decl_block.append("} // namespace ");
            forward_decl_block.append(scope);
            forward_decl_block.append("\n");
        }
    }
    if (!forward_decl_block.empty()) {
        forward_decl_block.append("\n");
    }

    std::vector<std::string> tag_array_names;
    std::vector<std::string> requirement_array_names;
    tag_array_names.reserve(cases.size());
    requirement_array_names.reserve(cases.size());

    std::string trait_declarations;
    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto       &test   = cases[idx];
        const std::string tag_id = "kCase" + std::to_string(idx) + "Tags";
        const std::string req_id = "kCase" + std::to_string(idx) + "Requirements";

        tag_array_names.push_back(tag_id);
        requirement_array_names.push_back(req_id);

        trait_declarations.append("constexpr std::array<std::string_view, ");
        trait_declarations.append(std::to_string(test.tags.size()));
        trait_declarations.append("> ");
        trait_declarations.append(tag_id);
        if (test.tags.empty()) {
            trait_declarations.append("{};\n");
        } else {
            trait_declarations.append(" = {\n");
            for (const auto &tag : test.tags) {
                trait_declarations.append("    \"");
                trait_declarations.append(escape_string(tag));
                trait_declarations.append("\",\n");
            }
            trait_declarations.append("};\n");
        }

        trait_declarations.append("constexpr std::array<std::string_view, ");
        trait_declarations.append(std::to_string(test.requirements.size()));
        trait_declarations.append("> ");
        trait_declarations.append(req_id);
        if (test.requirements.empty()) {
            trait_declarations.append("{};\n");
        } else {
            trait_declarations.append(" = {\n");
            for (const auto &req : test.requirements) {
                trait_declarations.append("    \"");
                trait_declarations.append(escape_string(req));
                trait_declarations.append("\",\n");
            }
            trait_declarations.append("};\n");
        }

        trait_declarations.append("\n");
    }

    std::string case_entries;
    if (cases.empty()) {
        case_entries = "    // No test cases discovered during code generation.\n";
    } else {
        case_entries.reserve(cases.size() * 128);
        for (std::size_t idx = 0; idx < cases.size(); ++idx) {
            const auto &test = cases[idx];
            case_entries.append("    Case{\n");
            case_entries.append("        \"");
            case_entries.append(escape_string(test.display_name));
            case_entries.append("\",\n");
            case_entries.append("        &");
            case_entries.append(test.qualified_name);
            case_entries.append(",\n");
            case_entries.append("        \"");
            case_entries.append(escape_string(test.filename));
            case_entries.append("\",\n");
            case_entries.append("        ");
            case_entries.append(std::to_string(test.line));
            case_entries.append(",\n");
            case_entries.append("        std::span{");
            case_entries.append(tag_array_names[idx]);
            case_entries.append("},\n");
            case_entries.append("        std::span{");
            case_entries.append(requirement_array_names[idx]);
            case_entries.append("},\n");
            case_entries.append("        ");
            if (!test.skip_reason.empty()) {
                case_entries.append("\"");
                case_entries.append(escape_string(test.skip_reason));
                case_entries.append("\"");
            } else {
                case_entries.append("std::string_view{}");
            }
            case_entries.append(",\n");
            case_entries.append("        ");
            case_entries.append(test.should_skip ? "true" : "false");
            case_entries.append("\n    },\n");
        }
    }

    std::string output = template_content;
    replace_all(output, "{{FORWARD_DECLS}}", forward_decl_block);
    replace_all(output, "{{CASE_COUNT}}", std::to_string(cases.size()));
    replace_all(output, "{{TRAIT_DECLS}}", trait_declarations);
    replace_all(output, "{{CASE_INITS}}", case_entries);
    replace_all(output, "{{ENTRY_FUNCTION}}", options.entry);

    return output;
}

CollectorOptions parse_arguments(int argc, const char **argv) {
    static llvm::cl::OptionCategory    category{"gentest codegen"};
    static llvm::cl::opt<std::string>  output_option{"output", llvm::cl::desc("Path to the output source file"), llvm::cl::Required,
                                                    llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  entry_option{"entry", llvm::cl::desc("Fully qualified entry point symbol"),
                                                   llvm::cl::init("gentest::run_all_tests"), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  compdb_option{"compdb", llvm::cl::desc("Directory containing compile_commands.json"),
                                                    llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::list<std::string> source_option{llvm::cl::Positional, llvm::cl::desc("Input source files"), llvm::cl::OneOrMore,
                                                     llvm::cl::cat(category)};
    static llvm::cl::list<std::string> clang_option{llvm::cl::ConsumeAfter, llvm::cl::desc("-- <clang arguments>")};
    static llvm::cl::opt<std::string>  template_option{"template", llvm::cl::desc("Path to the template file used for code generation"),
                                                      llvm::cl::init(""), llvm::cl::cat(category)};

    llvm::cl::HideUnrelatedOptions(category);
    llvm::cl::ParseCommandLineOptions(argc, argv, "gentest clang code generator\n");

    CollectorOptions opts;
    opts.entry       = entry_option;
    opts.output_path = std::filesystem::path{output_option.getValue()};
    opts.sources.assign(source_option.begin(), source_option.end());
    opts.clang_args.assign(clang_option.begin(), clang_option.end());
    if (!opts.clang_args.empty() && opts.clang_args.front() == "--") {
        opts.clang_args.erase(opts.clang_args.begin());
    }
    if (!compdb_option.getValue().empty()) {
        opts.compilation_database = std::filesystem::path{compdb_option.getValue()};
    }
    if (!template_option.getValue().empty()) {
        opts.template_path = std::filesystem::path{template_option.getValue()};
    } else if (!kTemplateDir.empty()) {
        opts.template_path = std::filesystem::path{kTemplateDir} / "test_impl.cpp.tpl";
    }
    return opts;
}

int emit(const CollectorOptions &opts, const std::vector<TestCaseInfo> &cases) {
    namespace fs      = std::filesystem;
    fs::path out_path = opts.output_path;
    if (out_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_path.parent_path(), ec);
        if (ec) {
            llvm::errs() << "gentest_codegen: failed to create directory '" << out_path.parent_path().string() << "': " << ec.message()
                         << "\n";
            return 1;
        }
    }

    if (opts.template_path.empty()) {
        llvm::errs() << "gentest_codegen: no template path configured" << '\n';
        return 1;
    }

    const auto content = render_cases(opts, cases);
    if (!content) {
        return 1;
    }

    std::ofstream file(out_path, std::ios::binary);
    if (!file) {
        llvm::errs() << "gentest_codegen: failed to open output file '" << out_path.string() << "'\n";
        return 1;
    }
    file << *content;
    file.close();
    return file ? 0 : 1;
}

} // namespace

int main(int argc, const char **argv) {
    const auto options = parse_arguments(argc, argv);

    std::unique_ptr<clang::tooling::CompilationDatabase> database;
    std::string                                          db_error;
    if (options.compilation_database) {
        database = clang::tooling::CompilationDatabase::loadFromDirectory(options.compilation_database->string(), db_error);
        if (!database) {
            llvm::errs() << "gentest_codegen: failed to load compilation database at '" << options.compilation_database->string()
                         << "': " << db_error << "\n";
            return 1;
        }
    } else {
        database = std::make_unique<clang::tooling::FixedCompilationDatabase>(".", std::vector<std::string>{});
    }

    clang::tooling::ClangTool tool{*database, options.sources};

    const auto extra_args           = options.clang_args;
    const auto default_include_dirs = detect_platform_include_dirs();

    if (options.compilation_database) {
        tool.appendArgumentsAdjuster(
            [extra_args, default_include_dirs](const clang::tooling::CommandLineArguments &command_line, llvm::StringRef) {
                clang::tooling::CommandLineArguments adjusted;
                if (!command_line.empty()) {
                    adjusted.emplace_back(command_line.front());
                    adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
                    for (const auto &dir : default_include_dirs) {
                        if (contains_isystem_entry(command_line, dir) || contains_isystem_entry(adjusted, dir)) {
                            continue;
                        }
                        adjusted.emplace_back("-isystem");
                        adjusted.emplace_back(dir);
                    }
                    for (std::size_t i = 1; i < command_line.size(); ++i) {
                        const auto &arg = command_line[i];
                        if (arg == "-fmodules-ts" || arg.rfind("-fmodule-mapper=", 0) == 0 || arg.rfind("-fdeps-format=", 0) == 0 ||
                            arg == "-fmodule-header") {
                            continue;
                        }
                        adjusted.push_back(arg);
                    }
                } else {
                    static constexpr std::string_view compiler = "clang++";
                    adjusted.emplace_back(compiler);
#if defined(__linux__)
                    adjusted.emplace_back("--gcc-toolchain=/usr");
#endif
                    adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
                    for (const auto &dir : default_include_dirs) {
                        if (contains_isystem_entry(command_line, dir) || contains_isystem_entry(adjusted, dir)) {
                            continue;
                        }
                        adjusted.emplace_back("-isystem");
                        adjusted.emplace_back(dir);
                    }
                }
                return adjusted;
            });
    } else {
        tool.appendArgumentsAdjuster(
            [extra_args, default_include_dirs](const clang::tooling::CommandLineArguments &command_line, llvm::StringRef) {
                clang::tooling::CommandLineArguments adjusted;
                static constexpr std::string_view    compiler = "clang++";
                adjusted.emplace_back(compiler);
#if defined(__linux__)
                adjusted.emplace_back("--gcc-toolchain=/usr");
#endif
                adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
                for (const auto &dir : default_include_dirs) {
                    if (contains_isystem_entry(command_line, dir) || contains_isystem_entry(adjusted, dir)) {
                        continue;
                    }
                    adjusted.emplace_back("-isystem");
                    adjusted.emplace_back(dir);
                }
                if (!command_line.empty()) {
                    for (std::size_t i = 1; i < command_line.size(); ++i) {
                        const auto &arg = command_line[i];
                        if (arg == "-fmodules-ts" || arg.rfind("-fmodule-mapper=", 0) == 0 || arg.rfind("-fdeps-format=", 0) == 0 ||
                            arg == "-fmodule-header") {
                            continue;
                        }
                        adjusted.push_back(arg);
                    }
                }
                return adjusted;
            });
    }

    tool.appendArgumentsAdjuster(clang::tooling::getClangSyntaxOnlyAdjuster());

    std::vector<TestCaseInfo> cases;
    TestCaseCollector         collector{cases};

    MatchFinder finder;
    finder.addMatcher(functionDecl(isDefinition()).bind("gentest.func"), &collector);

    const int status = tool.run(newFrontendActionFactory(&finder).get());
    if (status != 0) {
        return status;
    }
    if (collector.has_errors()) {
        return 1;
    }

    std::sort(cases.begin(), cases.end(),
              [](const TestCaseInfo &lhs, const TestCaseInfo &rhs) { return lhs.display_name < rhs.display_name; });

    return emit(options, cases);
}
