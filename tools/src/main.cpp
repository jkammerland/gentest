#include "discovery.hpp"
#include "emit.hpp"
#include "log.hpp"
#include "mock_discovery.hpp"
#include "model.hpp"
#include "parallel_for.hpp"
#include "tooling_support.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/Version.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <cstdlib>
#include <filesystem>
#include <fmt/core.h>
#include <iterator>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace clang;
using namespace clang::tooling;
using namespace clang::ast_matchers;
using gentest::codegen::CollectorOptions;
using gentest::codegen::FixtureDeclCollector;
using gentest::codegen::FixtureDeclInfo;
using gentest::codegen::TestCaseCollector;
using gentest::codegen::TestCaseInfo;
using gentest::codegen::MockUsageCollector;
using gentest::codegen::register_mock_matchers;
using gentest::codegen::resolve_free_fixtures;

#ifndef GENTEST_TEMPLATE_DIR
#define GENTEST_TEMPLATE_DIR ""
#endif
static constexpr std::string_view kTemplateDir = GENTEST_TEMPLATE_DIR;

namespace {

bool enforce_unique_base_names(std::vector<TestCaseInfo> &cases) {
    if (cases.empty()) {
        return true;
    }

    std::vector<std::size_t> order(cases.size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        order[i] = i;
    }
    std::ranges::sort(order, [&](std::size_t lhs, std::size_t rhs) {
        const auto &a = cases[lhs];
        const auto &b = cases[rhs];
        return std::tie(a.base_name, a.filename, a.line, a.display_name, a.qualified_name) <
            std::tie(b.base_name, b.filename, b.line, b.display_name, b.qualified_name);
    });

    std::unordered_map<std::string, std::string> first_location;
    std::unordered_set<std::string>              reported;
    std::vector<bool>                            keep(cases.size(), true);
    bool                                         ok = true;

    for (const auto idx : order) {
        const auto &c = cases[idx];
        if (c.base_name.empty()) {
            continue;
        }
        const std::string here = fmt::format("{}:{}", c.filename, c.line);
        auto              it   = first_location.find(c.base_name);
        if (it == first_location.end()) {
            first_location.emplace(c.base_name, here);
            continue;
        }
        if (it->second == here) {
            continue; // template instantiations from the same declaration
        }
        ok = false;
        keep[idx] = false;

        const std::string report_key = fmt::format("{}\n{}", c.base_name, here);
        if (reported.insert(report_key).second) {
            gentest::codegen::log_err("gentest_codegen: duplicate test name '{}' at {} (previously declared at {})\n", c.base_name, here,
                                      it->second);
        }
    }

    if (!ok) {
        std::vector<TestCaseInfo> filtered;
        filtered.reserve(cases.size());
        for (std::size_t i = 0; i < cases.size(); ++i) {
            if (keep[i]) {
                filtered.push_back(std::move(cases[i]));
            }
        }
        cases = std::move(filtered);
    }

    return ok;
}

bool should_strip_compdb_arg(std::string_view arg) {
    // CMake's experimental C++ modules support (and some GCC-based toolchains)
    // can inject GCC-only module/dependency scanning flags into compile commands.
    // Clang (which is embedded in our clang-tooling binary) rejects these.
    return arg == "-fmodules-ts" || arg == "-fmodule-header" || arg.starts_with("-fmodule-mapper=") ||
        arg.starts_with("-fdeps-format=") || arg.starts_with("-fdeps-file=") || arg.starts_with("-fdeps-target=") ||
        arg == "-fconcepts-diagnostics-depth" ||
        arg.starts_with("-fconcepts-diagnostics-depth=") ||
        // -Werror (and variants) are useful for real builds but make codegen brittle, because
        // warnings (unknown attributes/options) would abort parsing.
        arg == "-Werror" || arg.starts_with("-Werror=") || arg == "-pedantic-errors";
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

std::optional<std::size_t> parse_jobs_string(std::string_view raw_value) {
    llvm::StringRef value{raw_value.data(), raw_value.size()};
    value = value.trim();
    if (value.empty()) {
        return std::nullopt;
    }
    if (value.equals_insensitive("auto")) {
        return 0;
    }

    std::size_t out = 0;
    const auto  result = std::from_chars(value.begin(), value.end(), out);
    if (result.ec != std::errc{} || result.ptr != value.end()) {
        return std::nullopt;
    }
    return out;
}

std::string resolve_default_compiler_path() {
    static constexpr std::string_view kDefault = "clang++";
#if defined(_WIN32)
    static constexpr std::array<std::string_view, 2> kCandidates = {"clang++.exe", "clang++"};
#else
    const std::string versioned = std::string("clang++-") + std::to_string(CLANG_VERSION_MAJOR);
    const std::array<std::string, 2> kCandidates = {versioned, std::string(kDefault)};
#endif
    for (const auto &candidate : kCandidates) {
        auto path = llvm::sys::findProgramByName(candidate);
        if (path) {
            return *path;
        }
    }
    return std::string{kDefault};
}

bool has_resource_dir_arg(const std::vector<std::string> &args) {
    bool next_is_value = false;
    for (const auto &arg : args) {
        if (next_is_value) {
            return true;
        }
        if (arg == "-resource-dir") {
            next_is_value = true;
            continue;
        }
        if (arg.starts_with("-resource-dir=")) {
            return true;
        }
    }
    return false;
}

std::string resolve_resource_dir(const std::string &compiler_path) {
    if (compiler_path.empty()) {
        return {};
    }

    auto resolved_path = llvm::sys::findProgramByName(compiler_path);
    if (!resolved_path) {
        // `compiler_path` can be a full path already (or just not on PATH).
        // We'll still try to execute it and let ExecuteAndWait surface errors.
        resolved_path = compiler_path;
    }

    llvm::SmallString<128> tmp_path;
    int                    tmp_fd = -1;
    if (const auto ec = llvm::sys::fs::createTemporaryFile("gentest_codegen_resource_dir", "txt", tmp_fd, tmp_path)) {
        gentest::codegen::log_err("gentest_codegen: warning: failed to create temp file for resource-dir probe: {}\n", ec.message());
        return {};
    }
    (void)llvm::sys::Process::SafelyCloseFileDescriptor(tmp_fd);

    std::string tmp_path_str = tmp_path.str().str();
    llvm::StringRef tmp_path_ref{tmp_path_str};

    std::array<llvm::StringRef, 2> clang_args = {llvm::StringRef(*resolved_path), llvm::StringRef("-print-resource-dir")};
    std::array<std::optional<llvm::StringRef>, 3> redirects = {std::nullopt, tmp_path_ref, std::nullopt};

    std::string err_msg;
    const int   rc = llvm::sys::ExecuteAndWait(*resolved_path, clang_args, std::nullopt, redirects, 0, 0, &err_msg);
    if (rc != 0) {
        if (!err_msg.empty()) {
            gentest::codegen::log_err("gentest_codegen: warning: failed to query clang resource dir: {}\n", err_msg);
        }
        (void)llvm::sys::fs::remove(tmp_path_str);
        return {};
    }

    std::error_code io_ec;
    auto            in = llvm::MemoryBuffer::getFile(tmp_path_str);
    (void)llvm::sys::fs::remove(tmp_path_str);
    if (!in) {
        io_ec = in.getError();
        gentest::codegen::log_err("gentest_codegen: warning: failed to read clang resource dir output: {}\n", io_ec.message());
        return {};
    }

    std::string resource_dir = (*in)->getBuffer().str();
    auto        trimmed      = llvm::StringRef(resource_dir).trim();
    return trimmed.str();
}

CollectorOptions parse_arguments(int argc, const char **argv) {
    static llvm::cl::OptionCategory    category{"gentest codegen"};
    static llvm::cl::opt<std::string>  output_option{"output", llvm::cl::desc("Path to the output source file"), llvm::cl::init(""),
                                                    llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  entry_option{"entry", llvm::cl::desc("Fully qualified entry point symbol"),
                                                   llvm::cl::init("gentest::run_all_tests"), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  tu_out_dir_option{
        "tu-out-dir",
        llvm::cl::desc("Emit per-translation-unit wrapper .cpp/.h files into this directory (enables TU mode)"),
        llvm::cl::init(""),
        llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  compdb_option{"compdb", llvm::cl::desc("Directory containing compile_commands.json"),
                                                    llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  source_root_option{
        "source-root",
        llvm::cl::desc("Source root used to emit stable relative paths in gentest::Case.file"),
        llvm::cl::init(""),
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
    static llvm::cl::opt<unsigned>     jobs_option{
        "jobs",
        llvm::cl::desc("Max concurrency for TU wrapper mode parsing/emission (0=auto)"),
        llvm::cl::init(0),
        llvm::cl::cat(category)};
    static llvm::cl::list<std::string> source_option{llvm::cl::Positional, llvm::cl::desc("Input source files"), llvm::cl::OneOrMore,
                                                     llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  template_option{"template", llvm::cl::desc("Path to the template file used for code generation"),
                                                      llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  mock_registry_option{"mock-registry", llvm::cl::desc("Path to the generated mock registry header"),
                                                           llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  mock_impl_option{"mock-impl", llvm::cl::desc("Path to the generated mock implementation source"),
                                                       llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<bool> check_option{"check", llvm::cl::desc("Validate attributes only; do not emit code"), llvm::cl::init(false),
                                            llvm::cl::cat(category)};

    // Split tool args from trailing clang args after `--` ourselves because
    // llvm::cl positional parsing is otherwise prone to consuming everything.
    std::vector<const char*> tool_argv;
    tool_argv.reserve(static_cast<std::size_t>(argc));
    tool_argv.push_back(argv[0]);

    std::vector<std::string> clang_args;
    bool                     clang_mode = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        if (!clang_mode && arg == "--") {
            clang_mode = true;
            continue;
        }
        if (clang_mode) {
            clang_args.emplace_back(arg);
        } else {
            tool_argv.push_back(argv[i]);
        }
    }

    llvm::cl::HideUnrelatedOptions(category);
    llvm::cl::ParseCommandLineOptions(static_cast<int>(tool_argv.size()), tool_argv.data(), "gentest clang code generator\n");

    CollectorOptions opts;
    opts.entry       = entry_option;
    opts.output_path = std::filesystem::path{output_option.getValue()};
    if (!tu_out_dir_option.getValue().empty()) {
        opts.tu_output_dir = std::filesystem::path{tu_out_dir_option.getValue()};
    }
    opts.sources.assign(source_option.begin(), source_option.end());
    opts.clang_args = std::move(clang_args);
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
    opts.jobs = static_cast<std::size_t>(jobs_option.getValue());
    if (jobs_option.getNumOccurrences() == 0) {
        const auto jobs_env = get_env_value("GENTEST_CODEGEN_JOBS");
        if (jobs_env) {
            const auto parsed = parse_jobs_string(*jobs_env);
            if (!parsed) {
                gentest::codegen::log_err("gentest_codegen: warning: ignoring invalid GENTEST_CODEGEN_JOBS='{}'\n", *jobs_env);
            } else {
                opts.jobs = *parsed;
            }
        }
    }
    if (!mock_registry_option.getValue().empty()) {
        opts.mock_registry_path = std::filesystem::path{mock_registry_option.getValue()};
    }
    if (!mock_impl_option.getValue().empty()) {
        opts.mock_impl_path = std::filesystem::path{mock_impl_option.getValue()};
    }
    if (!compdb_option.getValue().empty()) {
        opts.compilation_database = std::filesystem::path{compdb_option.getValue()};
    }
    if (!source_root_option.getValue().empty()) {
        opts.source_root = std::filesystem::path{source_root_option.getValue()};
    }
    if (!template_option.getValue().empty()) {
        opts.template_path = std::filesystem::path{template_option.getValue()};
    } else if (!kTemplateDir.empty()) {
        opts.template_path = std::filesystem::path{std::string(kTemplateDir)} / "test_impl.cpp.tpl";
    }
    if (!opts.check_only && opts.output_path.empty() && opts.tu_output_dir.empty()) {
        gentest::codegen::log_err_raw("gentest_codegen: --output or --tu-out-dir is required unless --check is specified\n");
    }
    return opts;
}

} // namespace

int main(int argc, const char **argv) {
    const auto options = parse_arguments(argc, argv);
    const auto compiler_path = resolve_default_compiler_path();

    std::unique_ptr<clang::tooling::CompilationDatabase> database;
    std::string                                          db_error;
    if (options.compilation_database) {
        database = clang::tooling::CompilationDatabase::loadFromDirectory(options.compilation_database->string(), db_error);
        if (!database) {
            gentest::codegen::log_err("gentest_codegen: failed to load compilation database at '{}': {}\n",
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
    std::unique_ptr<clang::DiagnosticConsumer> diag_consumer;
    if (options.quiet_clang) {
        diag_consumer = std::make_unique<clang::IgnoringDiagConsumer>();
    } else {
#if CLANG_VERSION_MAJOR >= 21
        diag_consumer =
            std::make_unique<clang::TextDiagnosticPrinter>(llvm::errs(), diag_options, /*OwnsOutputStream=*/false);
#else
        diag_options = new clang::DiagnosticOptions();
        diag_consumer =
            std::make_unique<clang::TextDiagnosticPrinter>(llvm::errs(), diag_options.get(), /*OwnsOutputStream=*/false);
#endif
    }

    const auto extra_args = options.clang_args;
    const bool need_resource_dir = !has_resource_dir_arg(extra_args);
    const std::string resource_dir = need_resource_dir ? resolve_resource_dir(compiler_path) : std::string{};

    std::vector<TestCaseInfo>                    cases;
    std::vector<FixtureDeclInfo>                 fixtures;
    const bool                                   allow_includes = !options.tu_output_dir.empty();
    std::vector<gentest::codegen::MockClassInfo> mocks;

    const auto args_adjuster = [&]() -> clang::tooling::ArgumentsAdjuster {
        if (options.compilation_database) {
            const std::string compdb_dir = options.compilation_database->string();
            return [compiler_path, resource_dir, need_resource_dir, extra_args, compdb_dir](
                       const clang::tooling::CommandLineArguments &command_line, llvm::StringRef file) {
                clang::tooling::CommandLineArguments adjusted;
                if (!command_line.empty()) {
                    // Use compiler and flags from compilation database
                    adjusted.emplace_back(command_line.front());
                    if (need_resource_dir && !resource_dir.empty()) {
                        adjusted.emplace_back(std::string("-resource-dir=") + resource_dir);
                    }
                    adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
                    // Copy remaining args, filtering out C++ module flags
                    bool skip_next_arg = false;
                    for (std::size_t i = 1; i < command_line.size(); ++i) {
                        const auto &arg = command_line[i];
                        if (skip_next_arg) {
                            skip_next_arg = false;
                            continue;
                        }
                        if (arg == "-fmodule-mapper" || arg == "-fdeps-format" || arg == "-fdeps-file" || arg == "-fdeps-target" ||
                            arg == "-fconcepts-diagnostics-depth") {
                            skip_next_arg = true;
                            continue;
                        }
                        if (should_strip_compdb_arg(arg)) {
                            continue;
                        }
                        adjusted.push_back(arg);
                    }
                } else {
                    // No database entry found - create minimal synthetic command
                    // This shouldn't happen often, but is a fallback
                    gentest::codegen::log_err(
                        "gentest_codegen: warning: no compilation database entry for '{}'; using synthetic clang invocation "
                        "(compdb: '{}')\n",
                        file.str(), compdb_dir);
                    adjusted.emplace_back(compiler_path);
#if defined(__linux__)
                    adjusted.emplace_back("--gcc-toolchain=/usr");
#endif
                    if (need_resource_dir && !resource_dir.empty()) {
                        adjusted.emplace_back(std::string("-resource-dir=") + resource_dir);
                    }
                    adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
                }
                return adjusted;
            };
        }

        // No compilation database - use minimal synthetic command
        // User must provide include paths via extra_args (e.g., via -- -I/path/to/headers)
        return [compiler_path, resource_dir, need_resource_dir, extra_args](const clang::tooling::CommandLineArguments &command_line,
                                                                           llvm::StringRef) {
            clang::tooling::CommandLineArguments adjusted;
            adjusted.emplace_back(compiler_path);
#if defined(__linux__)
            adjusted.emplace_back("--gcc-toolchain=/usr");
#endif
            if (need_resource_dir && !resource_dir.empty()) {
                adjusted.emplace_back(std::string("-resource-dir=") + resource_dir);
            }
            adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
            if (!command_line.empty()) {
                bool skip_next_arg = false;
                for (std::size_t i = 1; i < command_line.size(); ++i) {
                    const auto &arg = command_line[i];
                    if (skip_next_arg) {
                        skip_next_arg = false;
                        continue;
                    }
                    if (arg == "-fmodule-mapper" || arg == "-fdeps-format" || arg == "-fdeps-file" || arg == "-fdeps-target" ||
                        arg == "-fconcepts-diagnostics-depth") {
                        skip_next_arg = true;
                        continue;
                    }
                    if (should_strip_compdb_arg(arg)) {
                        continue;
                    }
                    adjusted.push_back(arg);
                }
            }
            return adjusted;
        };
    }();

    const auto syntax_only_adjuster = clang::tooling::getClangSyntaxOnlyAdjuster();

    class SingleFileCompilationDatabase final : public clang::tooling::CompilationDatabase {
    public:
        explicit SingleFileCompilationDatabase(const std::vector<clang::tooling::CompileCommand> &commands) : commands_(commands) {}

        std::vector<clang::tooling::CompileCommand> getCompileCommands(llvm::StringRef /*file_path*/) const override {
            // Clang may query with a path spelling that differs from the original
            // source argument (slash direction, case, absolute/relative). In this
            // single-file wrapper mode we only ever serve one TU, so always return
            // that TU's compile command.
            return commands_;
        }

    private:
        const std::vector<clang::tooling::CompileCommand> &commands_;
    };

    struct ParseResult {
        int                                 status = 0;
        bool                                had_test_errors = false;
        bool                                had_fixture_errors = false;
        bool                                had_mock_errors = false;
        std::vector<TestCaseInfo>           cases;
        std::vector<FixtureDeclInfo>        fixtures;
        std::vector<gentest::codegen::MockClassInfo> mocks;
    };

    const std::size_t parse_jobs = gentest::codegen::resolve_concurrency(options.sources.size(), options.jobs);
    const bool        multi_tu   = allow_includes && options.sources.size() > 1;
    if (multi_tu) {
        // clang::tooling::JSONCompilationDatabase lazily builds internal maps. Accessing
        // it concurrently triggers TSAN reports (and is generally not guaranteed to be
        // thread-safe). Snapshot per-file compile commands up front so each worker can
        // run with an immutable database view.
        std::vector<std::vector<clang::tooling::CompileCommand>> compile_commands(options.sources.size());
        for (std::size_t i = 0; i < options.sources.size(); ++i) {
            compile_commands[i] = database->getCompileCommands(options.sources[i]);
        }

        std::vector<ParseResult> results(options.sources.size());
        std::vector<std::string> diag_texts(options.sources.size());

        const auto parse_one = [&](std::size_t idx) {
#if CLANG_VERSION_MAJOR < 21
            llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> tu_diag_options;
#else
            clang::DiagnosticOptions tu_diag_options;
#endif
            std::string             diag_buffer;
            llvm::raw_string_ostream diag_stream(diag_buffer);
            std::unique_ptr<clang::DiagnosticConsumer> tu_diag_consumer;
            if (options.quiet_clang) {
                tu_diag_consumer = std::make_unique<clang::IgnoringDiagConsumer>();
            } else {
#if CLANG_VERSION_MAJOR >= 21
                tu_diag_consumer =
                    std::make_unique<clang::TextDiagnosticPrinter>(diag_stream, tu_diag_options, /*OwnsOutputStream=*/false);
#else
                tu_diag_options = new clang::DiagnosticOptions();
                tu_diag_consumer = std::make_unique<clang::TextDiagnosticPrinter>(diag_stream, tu_diag_options.get(),
                                                                                  /*OwnsOutputStream=*/false);
#endif
            }

            const SingleFileCompilationDatabase file_database{compile_commands[idx]};

            // Use a per-tool physical filesystem instance. llvm::vfs::getRealFileSystem()
            // shares process working directory state and is documented as thread-hostile.
            auto physical_fs_unique = llvm::vfs::createPhysicalFileSystem();
            llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> base_fs;
            if (physical_fs_unique) {
                base_fs = llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>(physical_fs_unique.release());
            } else {
                base_fs = llvm::vfs::getRealFileSystem();
            }

            clang::tooling::ClangTool tool{
                file_database,
                std::vector<std::string>{options.sources[idx]},
                std::make_shared<clang::PCHContainerOperations>(),
                base_fs,
            };
            tool.setDiagnosticConsumer(tu_diag_consumer.get());
            tool.appendArgumentsAdjuster(args_adjuster);
            tool.appendArgumentsAdjuster(syntax_only_adjuster);

            std::vector<TestCaseInfo> local_cases;
            TestCaseCollector         collector{local_cases, options.strict_fixture, allow_includes};
            std::vector<FixtureDeclInfo> local_fixtures;
            FixtureDeclCollector         fixture_collector{local_fixtures};
            std::vector<gentest::codegen::MockClassInfo> local_mocks;
            MockUsageCollector                            mock_collector{local_mocks};

            MatchFinder finder;
            finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("gentest.func"), &collector);
            finder.addMatcher(cxxRecordDecl(isDefinition(), unless(isImplicit())).bind("gentest.fixture"), &fixture_collector);
            register_mock_matchers(finder, mock_collector);

            ParseResult result;
            result.status = tool.run(newFrontendActionFactory(&finder).get());
            result.had_test_errors = collector.has_errors();
            result.had_fixture_errors = fixture_collector.has_errors();
            result.had_mock_errors = mock_collector.has_errors();
            result.cases = std::move(local_cases);
            result.fixtures = std::move(local_fixtures);
            result.mocks = std::move(local_mocks);
            results[idx] = std::move(result);

            diag_stream.flush();
            diag_texts[idx] = std::move(diag_buffer);
        };

        // Some system LLVM/Clang builds are not TSAN-clean for first-use global
        // initialization. Run one TU serially to warm up internal singletons
        // before fanning out across worker threads.
        if (parse_jobs > 1) {
            parse_one(0);
            gentest::codegen::parallel_for(options.sources.size() - 1, parse_jobs, [&](std::size_t local_idx) {
                parse_one(local_idx + 1);
            });
        } else {
            for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
                parse_one(idx);
            }
        }

        int  status = 0;
        bool had_errors = false;
        for (const auto &text : diag_texts) {
            if (!text.empty()) {
                gentest::codegen::log_err_raw(text);
            }
        }
        for (auto &r : results) {
            if (status == 0 && r.status != 0) {
                status = r.status;
            }
            had_errors = had_errors || r.had_test_errors || r.had_fixture_errors || r.had_mock_errors;
            cases.insert(cases.end(), std::make_move_iterator(r.cases.begin()), std::make_move_iterator(r.cases.end()));
            fixtures.insert(fixtures.end(), std::make_move_iterator(r.fixtures.begin()), std::make_move_iterator(r.fixtures.end()));
            mocks.insert(mocks.end(), std::make_move_iterator(r.mocks.begin()), std::make_move_iterator(r.mocks.end()));
        }
        if (status != 0) {
            return status;
        }
        if (had_errors) {
            return 1;
        }
    } else {
        std::vector<clang::tooling::CompileCommand> single_file_commands;
        std::unique_ptr<SingleFileCompilationDatabase> single_file_database;
        if (options.compilation_database && options.sources.size() == 1) {
            single_file_commands = database->getCompileCommands(options.sources.front());
            single_file_database = std::make_unique<SingleFileCompilationDatabase>(single_file_commands);
        }

        clang::tooling::ClangTool tool{
            single_file_database ? static_cast<clang::tooling::CompilationDatabase &>(*single_file_database) : *database,
            options.sources};
        tool.setDiagnosticConsumer(diag_consumer.get());
        tool.appendArgumentsAdjuster(args_adjuster);
        tool.appendArgumentsAdjuster(syntax_only_adjuster);

        TestCaseCollector  collector{cases, options.strict_fixture, allow_includes};
        FixtureDeclCollector fixture_collector{fixtures};
        MockUsageCollector mock_collector{mocks};

        MatchFinder finder;
        finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("gentest.func"), &collector);
        finder.addMatcher(cxxRecordDecl(isDefinition(), unless(isImplicit())).bind("gentest.fixture"), &fixture_collector);
        register_mock_matchers(finder, mock_collector);

        const int status = tool.run(newFrontendActionFactory(&finder).get());
        if (status != 0) {
            return status;
        }
        if (collector.has_errors() || fixture_collector.has_errors() || mock_collector.has_errors()) {
            return 1;
        }
    }

    if (allow_includes) {
        if (!enforce_unique_base_names(cases)) {
            return 1;
        }
    }

    if (!resolve_free_fixtures(cases, fixtures)) {
        return 1;
    }

    std::ranges::sort(cases, {}, &TestCaseInfo::display_name);

    if (options.check_only) {
        return 0;
    }
    if (options.output_path.empty() && options.tu_output_dir.empty()) {
        gentest::codegen::log_err_raw("gentest_codegen: --output or --tu-out-dir is required unless --check is specified\n");
        return 1;
    }

    return gentest::codegen::emit(options, cases, fixtures, mocks);
}
