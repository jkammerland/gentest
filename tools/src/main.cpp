#include "discovery.hpp"
#include "emit.hpp"
#include "mock_discovery.hpp"
#include "model.hpp"
#include "tooling_support.hpp"

#include <algorithm>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/Version.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <cstdlib>
#include <filesystem>
#include <cctype>
#include <fmt/core.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace clang;
using namespace clang::tooling;
using namespace clang::ast_matchers;
using gentest::codegen::CollectorOptions;
using gentest::codegen::TestCaseCollector;
using gentest::codegen::TestCaseInfo;
using gentest::codegen::MockUsageCollector;
using gentest::codegen::register_mock_matchers;

#ifndef GENTEST_TEMPLATE_DIR
#define GENTEST_TEMPLATE_DIR ""
#endif
static constexpr std::string_view kTemplateDir = GENTEST_TEMPLATE_DIR;

namespace {

struct CapturedInclude {
    std::string spelling;
    std::string resolved_path;
    bool        is_angled = false;
};

namespace fs = std::filesystem;

fs::path normalize_path(const fs::path &path) {
    std::error_code ec;
    fs::path        out = path;
    if (!out.is_absolute()) {
        out = fs::absolute(out, ec);
        if (ec) {
            return path;
        }
    }
    ec.clear();
    out = fs::weakly_canonical(out, ec);
    if (ec) {
        return path;
    }
    return out;
}

bool is_header_include_target(std::string_view path) {
    const fs::path p{std::string(path)};
    std::string    ext = p.extension().string();
    std::string    lower;
    lower.resize(ext.size());
    std::transform(ext.begin(), ext.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.empty())
        return true;
    return !(lower == ".c" || lower == ".cc" || lower == ".cpp" || lower == ".cxx" || lower == ".c++" || lower == ".m" || lower == ".mm");
}

bool is_path_within(const fs::path &path, const fs::path &root) {
    if (root.empty())
        return false;
    auto path_it = path.begin();
    for (auto root_it = root.begin(); root_it != root.end(); ++root_it, ++path_it) {
        if (path_it == path.end() || *path_it != *root_it) {
            return false;
        }
    }
    return true;
}

void collect_include_roots_from_args(const std::vector<std::string> &args, const fs::path &base_dir,
                                    std::vector<fs::path> &roots, std::set<std::string> &seen) {
    auto add_root = [&](std::string_view value) {
        if (value.empty())
            return;
        fs::path p{std::string(value)};
        if (!p.is_absolute()) {
            p = base_dir / p;
        }
        p = normalize_path(p);
        const auto key = p.generic_string();
        if (seen.insert(key).second) {
            roots.push_back(std::move(p));
        }
    };

    auto maybe_add_next = [&](std::size_t &i) {
        if (i + 1 >= args.size())
            return;
        add_root(args[i + 1]);
        ++i;
    };

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "-I" || arg == "-isystem" || arg == "-iquote" || arg == "-idirafter") {
            maybe_add_next(i);
            continue;
        }
        if (arg == "/I") {
            maybe_add_next(i);
            continue;
        }
        if (arg.rfind("-I", 0) == 0 && arg.size() > 2) {
            add_root(arg.substr(2));
            continue;
        }
        if (arg.rfind("-isystem", 0) == 0 && arg.size() > 8) {
            add_root(arg.substr(8));
            continue;
        }
        if (arg.rfind("-iquote", 0) == 0 && arg.size() > 7) {
            add_root(arg.substr(7));
            continue;
        }
        if (arg.rfind("-idirafter", 0) == 0 && arg.size() > 10) {
            add_root(arg.substr(10));
            continue;
        }
        if (arg.rfind("/I", 0) == 0 && arg.size() > 2) {
            add_root(arg.substr(2));
            continue;
        }
    }
}

std::vector<std::string> collect_pp_flags_from_args(const std::vector<std::string> &args, const fs::path &base_dir) {
    std::vector<std::string> flags;

    auto normalize_value_path = [&](std::string_view value) -> std::string {
        if (value.empty()) {
            return {};
        }
        fs::path p{std::string(value)};
        if (!p.is_absolute()) {
            p = base_dir / p;
        }
        p = normalize_path(p);
        return p.generic_string();
    };

    auto maybe_add_next = [&](std::size_t &i, std::string_view key) {
        if (i + 1 >= args.size()) {
            return;
        }
        const std::string normalized = normalize_value_path(args[i + 1]);
        flags.push_back(fmt::format("{}={}", key, normalized));
        ++i;
    };

    auto add_define = [&](std::string_view value) {
        if (value.empty()) {
            return;
        }
        flags.push_back(std::string("-D") + std::string(value));
    };

    auto add_undef = [&](std::string_view value) {
        if (value.empty()) {
            return;
        }
        flags.push_back(std::string("-U") + std::string(value));
    };

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (arg == "-I") {
            maybe_add_next(i, "-I");
            continue;
        }
        if (arg == "-isystem") {
            maybe_add_next(i, "-isystem");
            continue;
        }
        if (arg == "-iquote") {
            maybe_add_next(i, "-iquote");
            continue;
        }
        if (arg == "-idirafter") {
            maybe_add_next(i, "-idirafter");
            continue;
        }
        if (arg == "--sysroot") {
            maybe_add_next(i, "--sysroot");
            continue;
        }
        if (arg == "-isysroot") {
            maybe_add_next(i, "-isysroot");
            continue;
        }
        if (arg == "-D") {
            if (i + 1 < args.size()) {
                add_define(args[i + 1]);
                ++i;
            }
            continue;
        }
        if (arg == "-U") {
            if (i + 1 < args.size()) {
                add_undef(args[i + 1]);
                ++i;
            }
            continue;
        }
        if (arg == "/I") {
            maybe_add_next(i, "/I");
            continue;
        }
        if (arg == "/D") {
            if (i + 1 < args.size()) {
                add_define(args[i + 1]);
                ++i;
            }
            continue;
        }
        if (arg == "/U") {
            if (i + 1 < args.size()) {
                add_undef(args[i + 1]);
                ++i;
            }
            continue;
        }
        if (arg.rfind("-I", 0) == 0 && arg.size() > 2) {
            flags.push_back(fmt::format("-I={}", normalize_value_path(arg.substr(2))));
            continue;
        }
        if (arg.rfind("-isystem", 0) == 0 && arg.size() > 8) {
            flags.push_back(fmt::format("-isystem={}", normalize_value_path(arg.substr(8))));
            continue;
        }
        if (arg.rfind("-iquote", 0) == 0 && arg.size() > 7) {
            flags.push_back(fmt::format("-iquote={}", normalize_value_path(arg.substr(7))));
            continue;
        }
        if (arg.rfind("-idirafter", 0) == 0 && arg.size() > 10) {
            flags.push_back(fmt::format("-idirafter={}", normalize_value_path(arg.substr(10))));
            continue;
        }
        if (arg.rfind("-D", 0) == 0 && arg.size() > 2) {
            add_define(arg.substr(2));
            continue;
        }
        if (arg.rfind("-U", 0) == 0 && arg.size() > 2) {
            add_undef(arg.substr(2));
            continue;
        }
        if (arg.rfind("--sysroot=", 0) == 0 && arg.size() > 10) {
            flags.push_back(fmt::format("--sysroot={}", normalize_value_path(arg.substr(10))));
            continue;
        }
        if (arg.rfind("-isysroot", 0) == 0 && arg.size() > 9) {
            flags.push_back(fmt::format("-isysroot={}", normalize_value_path(arg.substr(9))));
            continue;
        }
        if (arg.rfind("/I", 0) == 0 && arg.size() > 2) {
            flags.push_back(fmt::format("/I={}", normalize_value_path(arg.substr(2))));
            continue;
        }
        if (arg.rfind("/D", 0) == 0 && arg.size() > 2) {
            add_define(arg.substr(2));
            continue;
        }
        if (arg.rfind("/U", 0) == 0 && arg.size() > 2) {
            add_undef(arg.substr(2));
            continue;
        }
    }

    return flags;
}

bool validate_consistent_pp_flags(const clang::tooling::CompilationDatabase &database, const CollectorOptions &options) {
    if (!options.compilation_database) {
        return true;
    }
    if (options.sources.size() < 2) {
        return true;
    }

    std::optional<std::vector<std::string>> baseline;
    std::string                             baseline_source;

    // Prefer a source with exactly one compile command as baseline. This avoids
    // ambiguity when a file is compiled into multiple targets (and therefore
    // appears multiple times in compile_commands.json).
    for (const auto &source : options.sources) {
        const auto commands = database.getCompileCommands(source);
        if (commands.size() != 1) {
            continue;
        }
        const fs::path base_dir{commands.front().Directory};
        baseline        = collect_pp_flags_from_args(commands.front().CommandLine, base_dir);
        baseline_source = source;
        break;
    }
    if (!baseline.has_value()) {
        for (const auto &source : options.sources) {
            const auto commands = database.getCompileCommands(source);
            if (commands.empty()) {
                continue;
            }
            const fs::path base_dir{commands.front().Directory};
            baseline        = collect_pp_flags_from_args(commands.front().CommandLine, base_dir);
            baseline_source = source;
            break;
        }
    }
    if (!baseline.has_value()) {
        return true;
    }

    for (const auto &source : options.sources) {
        const auto commands = database.getCompileCommands(source);
        if (commands.empty()) {
            continue;
        }
        bool matched = false;
        for (const auto &command : commands) {
            const fs::path base_dir{command.Directory};
            const auto     flags = collect_pp_flags_from_args(command.CommandLine, base_dir);
            if (flags == *baseline) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            llvm::errs() << "gentest_codegen: per-source preprocessor flags differ across scanned sources; this is not supported\n";
            llvm::errs() << fmt::format("gentest_codegen: baseline source: {}\n", baseline_source);
            llvm::errs() << fmt::format("gentest_codegen: mismatching source: {}\n", source);
            llvm::errs() << "gentest_codegen: move per-source -I/-D flags to target-level (target_include_directories/target_compile_definitions)\n";
            return false;
        }
    }

    return true;
}

std::vector<fs::path> collect_include_roots(const clang::tooling::CompilationDatabase &database, const CollectorOptions &options) {
    std::vector<fs::path> roots;
    std::set<std::string> seen;

    // Prefer roots from the compilation database (per-source commands); also add any
    // extra args provided to gentest_codegen, plus user-provided include roots as a
    // fallback for non-compdb invocations.
    for (const auto &source : options.sources) {
        const auto commands = database.getCompileCommands(source);
        if (commands.empty())
            continue;
        const fs::path base_dir{commands.front().Directory};
        collect_include_roots_from_args(commands.front().CommandLine, base_dir, roots, seen);
    }

    collect_include_roots_from_args(options.clang_args, fs::current_path(), roots, seen);

    for (const auto &root : options.include_roots) {
        if (root.empty())
            continue;
        const fs::path normalized = normalize_path(root);
        const auto     key        = normalized.generic_string();
        if (seen.insert(key).second) {
            roots.push_back(normalized);
        }
    }

    return roots;
}

std::string make_include_line(const CapturedInclude &include, const std::vector<fs::path> &roots) {
    const char open  = include.is_angled ? '<' : '"';
    const char close = include.is_angled ? '>' : '"';

    std::string path = include.spelling;
    if (!include.is_angled && !include.resolved_path.empty()) {
        const fs::path resolved = normalize_path(include.resolved_path);
        std::string   best;
        for (const auto &root : roots) {
            if (!is_path_within(resolved, root))
                continue;
            const fs::path rel = resolved.lexically_relative(root);
            if (rel.empty() || rel.is_absolute())
                continue;
            const std::string candidate = rel.generic_string();
            if (best.empty() || candidate.size() < best.size()) {
                best = candidate;
            }
        }
        if (!best.empty()) {
            path = std::move(best);
        }
    }

    std::string line;
    line.reserve(path.size() + 12);
    line += "#include ";
    line += open;
    line += path;
    line += close;
    return line;
}

std::optional<std::string> get_env_value(std::string_view name) {
    std::string name_str{name};
#if defined(_WIN32)
    char  *env_value = nullptr;
    size_t env_len   = 0;
    if (_dupenv_s(&env_value, &env_len, name_str.c_str()) != 0 || env_value == nullptr) {
        return std::nullopt;
    }
    std::string value{env_value};
    std::free(env_value);
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
#else
    const char *env_value = std::getenv(name_str.c_str());
    if (env_value == nullptr || *env_value == '\0') {
        return std::nullopt;
    }
    return std::string{env_value};
#endif
}

CollectorOptions parse_arguments(int argc, const char **argv) {
    static llvm::cl::OptionCategory    category{"gentest codegen"};
    static llvm::cl::opt<std::string>  output_option{"output", llvm::cl::desc("Path to the output source file"), llvm::cl::init(""),
                                                    llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  entry_option{"entry", llvm::cl::desc("Fully qualified entry point symbol"),
                                                   llvm::cl::init("gentest::run_all_tests"), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  compdb_option{"compdb", llvm::cl::desc("Directory containing compile_commands.json"),
                                                    llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::list<std::string> include_root_option{
        "include-root",
        llvm::cl::desc("Path prefix used to emit relative #include directives for input sources (repeatable)"),
        llvm::cl::ZeroOrMore,
        llvm::cl::cat(category)};
    static llvm::cl::opt<bool>         no_include_sources_option{
        "no-include-sources",
        llvm::cl::desc("Do not emit #include directives for input sources (deprecated env: GENTEST_NO_INCLUDE_SOURCES)"),
        llvm::cl::init(false),
        llvm::cl::cat(category)};
    static llvm::cl::opt<bool>         strict_fixture_option{
        "strict-fixture",
        llvm::cl::desc("Treat member tests on suite/global fixtures as errors (deprecated env: GENTEST_STRICT_FIXTURE)"),
        llvm::cl::init(false),
        llvm::cl::cat(category)};
    static llvm::cl::opt<bool>         quiet_clang_option{
        "quiet-clang",
        llvm::cl::desc("Suppress clang diagnostics"),
        llvm::cl::init(false),
        llvm::cl::cat(category)};
    static llvm::cl::list<std::string> source_option{llvm::cl::Positional, llvm::cl::desc("Input source files"), llvm::cl::OneOrMore,
                                                     llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  template_option{"template", llvm::cl::desc("Path to the template file used for code generation"),
                                                      llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  mock_registry_option{"mock-registry", llvm::cl::desc("Path to the generated mock registry header"),
                                                           llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  mock_impl_option{"mock-impl", llvm::cl::desc("Path to the generated mock implementation source"),
                                                       llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  test_decls_option{"test-decls",
                                                        llvm::cl::desc("Path to the generated test declarations header"),
                                                        llvm::cl::init(""),
                                                        llvm::cl::cat(category)};
    static llvm::cl::opt<bool> check_option{"check", llvm::cl::desc("Validate attributes only; do not emit code"), llvm::cl::init(false),
                                            llvm::cl::cat(category)};

    llvm::cl::HideUnrelatedOptions(category);
    // Manually split args at `--` so `clang_args` never steals positional
    // sources. This keeps `gentest_codegen a.cpp b.cpp -- -I...` stable even
    // when multiple source paths are provided.
    std::vector<std::string> trailing_clang_args;
    int                      parse_argc = argc;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--") {
            for (int j = i + 1; j < argc; ++j) {
                trailing_clang_args.emplace_back(argv[j]);
            }
            parse_argc = i;
            break;
        }
    }
    llvm::cl::ParseCommandLineOptions(parse_argc, argv, "gentest clang code generator\n");

    CollectorOptions opts;
    opts.entry       = entry_option;
    opts.output_path = std::filesystem::path{output_option.getValue()};
    opts.sources.assign(source_option.begin(), source_option.end());
    opts.include_roots.reserve(include_root_option.size());
    for (const auto &root : include_root_option) {
        opts.include_roots.emplace_back(std::filesystem::path{root});
    }
    opts.clang_args = std::move(trailing_clang_args);
    opts.check_only = check_option.getValue();
    opts.quiet_clang = quiet_clang_option.getValue();
    opts.strict_fixture = [&] {
        if (strict_fixture_option.getValue()) {
            return true;
        }
        const auto strict_env = get_env_value("GENTEST_STRICT_FIXTURE");
        return strict_env && *strict_env != "0";
    }();
    opts.include_sources = [&] {
        if (no_include_sources_option.getValue()) {
            return false;
        }
        const auto no_inc_env = get_env_value("GENTEST_NO_INCLUDE_SOURCES");
        const bool skip_env   = (no_inc_env && *no_inc_env != "0");
        return !skip_env;
    }();
    if (!mock_registry_option.getValue().empty()) {
        opts.mock_registry_path = std::filesystem::path{mock_registry_option.getValue()};
    }
    if (!mock_impl_option.getValue().empty()) {
        opts.mock_impl_path = std::filesystem::path{mock_impl_option.getValue()};
    }
    if (!test_decls_option.getValue().empty()) {
        opts.test_decls_path = std::filesystem::path{test_decls_option.getValue()};
    }
    if (!compdb_option.getValue().empty()) {
        opts.compilation_database = std::filesystem::path{compdb_option.getValue()};
    }
    if (!template_option.getValue().empty()) {
        opts.template_path = std::filesystem::path{template_option.getValue()};
    } else if (!kTemplateDir.empty()) {
        opts.template_path = std::filesystem::path{std::string(kTemplateDir)} / "test_impl.cpp.tpl";
    }
    if (!opts.check_only && opts.output_path.empty()) {
        llvm::errs() << "gentest_codegen: --output is required unless --check is specified\n";
    }
    if (!opts.check_only && opts.test_decls_path.empty()) {
        llvm::errs() << "gentest_codegen: --test-decls is required unless --check is specified\n";
    }
    return opts;
}

class IncludeCapturingCallbacks : public clang::PPCallbacks {
public:
    IncludeCapturingCallbacks(clang::SourceManager &sm, std::vector<CapturedInclude> &out) : sm_(sm), out_(out) {}

    void InclusionDirective(clang::SourceLocation HashLoc, const clang::Token &, llvm::StringRef FileName, bool IsAngled,
                            clang::CharSourceRange, clang::OptionalFileEntryRef File, llvm::StringRef, llvm::StringRef,
                            const clang::Module *, bool, clang::SrcMgr::CharacteristicKind) override {
        if (!sm_.isWrittenInMainFile(HashLoc)) {
            return;
        }
        if (FileName.empty()) {
            return;
        }
        if (FileName == "gentest/mock.h") {
            return;
        }

        CapturedInclude inc;
        inc.spelling  = FileName.str();
        inc.is_angled = IsAngled;
        if (File) {
            inc.resolved_path = std::string(File->getName());
        }
        out_.push_back(std::move(inc));
    }

private:
    clang::SourceManager &        sm_;
    std::vector<CapturedInclude> &out_;
};

class IncludeCapturingAction : public clang::ASTFrontendAction {
public:
    IncludeCapturingAction(clang::ast_matchers::MatchFinder &finder, std::vector<CapturedInclude> &includes)
        : finder_(finder), includes_(includes) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &ci, llvm::StringRef) override {
        ci.getPreprocessor().addPPCallbacks(std::make_unique<IncludeCapturingCallbacks>(ci.getSourceManager(), includes_));
        return finder_.newASTConsumer();
    }

private:
    clang::ast_matchers::MatchFinder &finder_;
    std::vector<CapturedInclude> &    includes_;
};

class IncludeCapturingFactory : public clang::tooling::FrontendActionFactory {
public:
    IncludeCapturingFactory(clang::ast_matchers::MatchFinder &finder, std::vector<CapturedInclude> &includes)
        : finder_(finder), includes_(includes) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<IncludeCapturingAction>(finder_, includes_);
    }

private:
    clang::ast_matchers::MatchFinder &finder_;
    std::vector<CapturedInclude> &    includes_;
};

} // namespace

int main(int argc, const char **argv) {
    const auto options = parse_arguments(argc, argv);

    std::unique_ptr<clang::tooling::CompilationDatabase> database;
    std::string                                          db_error;
    if (options.compilation_database) {
        database = clang::tooling::CompilationDatabase::loadFromDirectory(options.compilation_database->string(), db_error);
        if (!database) {
            llvm::errs() << fmt::format("gentest_codegen: failed to load compilation database at '{}': {}\n",
                                        options.compilation_database->string(), db_error);
            return 1;
        }
    } else {
        database = std::make_unique<clang::tooling::FixedCompilationDatabase>(".", std::vector<std::string>{});
    }

#if CLANG_VERSION_MAJOR < 21
    llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> diag_options;
#else
    clang::DiagnosticOptions diag_options;
#endif

    const auto extra_args = options.clang_args;
    if (!validate_consistent_pp_flags(*database, options)) {
        return 1;
    }

    std::vector<TestCaseInfo>                    cases;
    TestCaseCollector                            collector{cases, options.strict_fixture};
    std::vector<gentest::codegen::MockClassInfo> mocks;
    MockUsageCollector                           mock_collector{mocks};
    std::vector<CapturedInclude>                 includes;

    MatchFinder finder;
    finder.addMatcher(functionDecl().bind("gentest.func"), &collector);
    register_mock_matchers(finder, mock_collector);

    const auto factory = std::make_unique<IncludeCapturingFactory>(finder, includes);
    for (const auto &source : options.sources) {
        clang::tooling::ClangTool tool{*database, std::vector<std::string>{source}};
        if (options.quiet_clang) {
            tool.setDiagnosticConsumer(new clang::IgnoringDiagConsumer());
        } else {
#if CLANG_VERSION_MAJOR >= 21
            tool.setDiagnosticConsumer(new clang::TextDiagnosticPrinter(llvm::errs(), diag_options, /*OwnsOutputStream=*/false));
#else
            diag_options = new clang::DiagnosticOptions();
            tool.setDiagnosticConsumer(new clang::TextDiagnosticPrinter(llvm::errs(), diag_options.get(), /*OwnsOutputStream=*/false));
#endif
        }

        if (options.compilation_database) {
            tool.appendArgumentsAdjuster(
                [extra_args](const clang::tooling::CommandLineArguments &command_line, llvm::StringRef) {
                    clang::tooling::CommandLineArguments adjusted;
                    if (!command_line.empty()) {
                        // Use compiler and flags from compilation database
                        adjusted.emplace_back(command_line.front());
                        adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
                        // Copy remaining args, filtering out C++ module flags
                        for (std::size_t i = 1; i < command_line.size(); ++i) {
                            const auto &arg = command_line[i];
                            if (arg == "-fmodules-ts" || arg.rfind("-fmodule-mapper=", 0) == 0 ||
                                arg.rfind("-fdeps-format=", 0) == 0 || arg == "-fmodule-header") {
                                continue;
                            }
                            adjusted.push_back(arg);
                        }
                    } else {
                        // No database entry found - create minimal synthetic command
                        // This shouldn't happen often, but is a fallback
                        static constexpr std::string_view compiler = "clang++";
                        adjusted.emplace_back(compiler);
#if defined(__linux__)
                        adjusted.emplace_back("--gcc-toolchain=/usr");
#endif
                        adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
                    }
                    return adjusted;
                });
        } else {
            // No compilation database - use minimal synthetic command
            // User must provide include paths via extra_args (e.g., via -- -I/path/to/headers)
            tool.appendArgumentsAdjuster(
                [extra_args](const clang::tooling::CommandLineArguments &command_line, llvm::StringRef) {
                    clang::tooling::CommandLineArguments adjusted;
                    static constexpr std::string_view    compiler = "clang++";
                    adjusted.emplace_back(compiler);
#if defined(__linux__)
                    adjusted.emplace_back("--gcc-toolchain=/usr");
#endif
                    adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
                    if (!command_line.empty()) {
                        for (std::size_t i = 1; i < command_line.size(); ++i) {
                            const auto &arg = command_line[i];
                            if (arg == "-fmodules-ts" || arg.rfind("-fmodule-mapper=", 0) == 0 ||
                                arg.rfind("-fdeps-format=", 0) == 0 || arg == "-fmodule-header") {
                                continue;
                            }
                            adjusted.push_back(arg);
                        }
                    }
                    return adjusted;
                });
        }

        tool.appendArgumentsAdjuster(clang::tooling::getClangSyntaxOnlyAdjuster());

        const int status = tool.run(factory.get());
        if (status != 0) {
            return status;
        }
    }
    if (collector.has_errors() || mock_collector.has_errors()) {
        return 1;
    }

    std::sort(cases.begin(), cases.end(),
              [](const TestCaseInfo &lhs, const TestCaseInfo &rhs) { return lhs.display_name < rhs.display_name; });

    if (options.check_only) {
        return 0;
    }
    if (options.output_path.empty()) {
        llvm::errs() << fmt::format("gentest_codegen: --output is required unless --check is specified\n");
        return 1;
    }
    if (options.test_decls_path.empty()) {
        llvm::errs() << fmt::format("gentest_codegen: --test-decls is required unless --check is specified\n");
        return 1;
    }

    std::vector<std::string> include_lines;
    include_lines.reserve(includes.size());

    const auto roots = collect_include_roots(*database, options);

    std::set<std::string> seen_include_lines;
    for (const auto &inc : includes) {
        const std::string_view target = inc.resolved_path.empty() ? std::string_view{inc.spelling} : std::string_view{inc.resolved_path};
        if (!is_header_include_target(target)) {
            continue;
        }

        auto line = make_include_line(inc, roots);
        if (seen_include_lines.insert(line).second) {
            include_lines.push_back(std::move(line));
        }
    }

    return gentest::codegen::emit(options, cases, mocks, include_lines);
}
