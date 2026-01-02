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
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <cstdlib>
#include <filesystem>
#include <fmt/core.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <optional>
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
    static llvm::cl::list<std::string> clang_option{llvm::cl::ConsumeAfter, llvm::cl::desc("-- <clang arguments>")};
    static llvm::cl::opt<std::string>  template_option{"template", llvm::cl::desc("Path to the template file used for code generation"),
                                                      llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  mock_registry_option{"mock-registry", llvm::cl::desc("Path to the generated mock registry header"),
                                                           llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  mock_impl_option{"mock-impl", llvm::cl::desc("Path to the generated mock implementation source"),
                                                       llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<bool> check_option{"check", llvm::cl::desc("Validate attributes only; do not emit code"), llvm::cl::init(false),
                                            llvm::cl::cat(category)};

    llvm::cl::HideUnrelatedOptions(category);
    llvm::cl::ParseCommandLineOptions(argc, argv, "gentest clang code generator\n");

    CollectorOptions opts;
    opts.entry       = entry_option;
    opts.output_path = std::filesystem::path{output_option.getValue()};
    opts.sources.assign(source_option.begin(), source_option.end());
    opts.clang_args.assign(clang_option.begin(), clang_option.end());
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
    if (!opts.clang_args.empty() && opts.clang_args.front() == "--") {
        opts.clang_args.erase(opts.clang_args.begin());
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
    return opts;
}

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
    clang::tooling::ClangTool tool{*database, options.sources};
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

    const auto extra_args = options.clang_args;

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
                        if (arg == "-fmodules-ts" || arg.rfind("-fmodule-mapper=", 0) == 0 || arg.rfind("-fdeps-format=", 0) == 0 ||
                            arg == "-fmodule-header") {
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

    std::vector<TestCaseInfo>                    cases;
    TestCaseCollector                            collector{cases, options.strict_fixture};
    std::vector<gentest::codegen::MockClassInfo> mocks;
    MockUsageCollector                           mock_collector{mocks};

    MatchFinder finder;
    finder.addMatcher(functionDecl(isDefinition()).bind("gentest.func"), &collector);
    register_mock_matchers(finder, mock_collector);

    const int status = tool.run(newFrontendActionFactory(&finder).get());
    if (status != 0) {
        return status;
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

    return gentest::codegen::emit(options, cases, mocks);
}
