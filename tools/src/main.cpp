#include <algorithm>
#include <array>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <cstddef>
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

struct CollectorOptions {
    std::string                          entry = "gentest::run_all_tests";
    std::filesystem::path                output_path;
    std::filesystem::path                template_path;
    std::vector<std::string>             sources;
    std::vector<std::string>             clang_args;
    std::optional<std::filesystem::path> compilation_database;
};

struct TestCaseInfo {
    std::string qualified_name;
    std::string display_name;
    std::string filename;
    unsigned    line = 0;
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
    static constexpr std::string_view kCasePrefix = "gentest::case:";

    static std::optional<TestCaseInfo> classify(const FunctionDecl &func, const SourceManager &sm, const LangOptions &lang) {
        std::optional<std::string> display;

        for (const auto *attr : func.specific_attrs<AnnotateAttr>()) {
            llvm::StringRef annotation = attr->getAnnotation();
            if (!annotation.starts_with(kCasePrefix)) {
                continue;
            }
            llvm::StringRef value = annotation.drop_front(kCasePrefix.size());
            if (!value.empty()) {
                display = value.str();
            }
        }

        if (!display.has_value()) {
            // Fallback to qualified name if annotation missing
            display = func.getQualifiedNameAsString();
            if (display->empty()) {
                display = func.getNameAsString();
            }
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
        info.display_name   = std::move(*display);
        info.filename       = filename.str();
        info.line           = line;
        return info;
    }

    std::vector<TestCaseInfo>                                         &out_;
    std::set<std::pair<std::string, std::pair<std::string, unsigned>>> seen_;
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

    std::string case_entries;
    if (cases.empty()) {
        case_entries = "    // No test cases discovered during code generation.\n";
    } else {
        case_entries.reserve(cases.size() * 64);
        for (const auto &test : cases) {
            case_entries.append("    Case{\"");
            case_entries.append(escape_string(test.display_name));
            case_entries.append("\", &");
            case_entries.append(test.qualified_name);
            case_entries.append(", \"");
            case_entries.append(escape_string(test.filename));
            case_entries.append("\", ");
            case_entries.append(std::to_string(test.line));
            case_entries.append("},\n");
        }
    }

    std::string output = template_content;
    replace_all(output, "{{FORWARD_DECLS}}", forward_decl_block);
    replace_all(output, "{{CASE_COUNT}}", std::to_string(cases.size()));
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

    const auto                                   extra_args = options.clang_args;
    static const std::array<std::string_view, 4> default_system_includes{
        "/usr/lib/gcc/x86_64-redhat-linux/15/../../../../include/c++/15",
        "/usr/lib/gcc/x86_64-redhat-linux/15/../../../../include/c++/15/x86_64-redhat-linux",
        "/usr/lib/gcc/x86_64-redhat-linux/15/../../../../include/c++/15/backward", "/usr/lib/gcc/x86_64-redhat-linux/15/include"};

    if (options.compilation_database) {
        tool.appendArgumentsAdjuster([extra_args](const clang::tooling::CommandLineArguments &command_line, llvm::StringRef) {
            clang::tooling::CommandLineArguments adjusted;
            static constexpr std::string_view    compiler = "clang++";
            if (!command_line.empty()) {
                adjusted.emplace_back(compiler);
                adjusted.emplace_back("--gcc-toolchain=/usr");
                adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
                for (const auto &dir : default_system_includes) {
                    adjusted.emplace_back("-isystem");
                    adjusted.emplace_back(std::string(dir));
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
                adjusted.emplace_back(compiler);
                adjusted.emplace_back("--gcc-toolchain=/usr");
                adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
                for (const auto &dir : default_system_includes) {
                    adjusted.emplace_back("-isystem");
                    adjusted.emplace_back(std::string(dir));
                }
            }
            return adjusted;
        });
    } else {
        tool.appendArgumentsAdjuster([extra_args](const clang::tooling::CommandLineArguments &command_line, llvm::StringRef) {
            clang::tooling::CommandLineArguments adjusted;
            static constexpr std::string_view    compiler = "clang++";
            adjusted.emplace_back(compiler);
            adjusted.emplace_back("--gcc-toolchain=/usr");
            adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
            for (const auto &dir : default_system_includes) {
                adjusted.emplace_back("-isystem");
                adjusted.emplace_back(std::string(dir));
            }
            if (!command_line.empty()) {
                adjusted.insert(adjusted.end(), command_line.begin() + 1, command_line.end());
            }
            return adjusted;
        });
    }

    tool.appendArgumentsAdjuster(clang::tooling::getClangSyntaxOnlyAdjuster());

    std::vector<TestCaseInfo> cases;
    TestCaseCollector         collector{cases};

    MatchFinder finder;
    finder.addMatcher(functionDecl(isDefinition(), hasAttr(attr::Annotate)).bind("gentest.func"), &collector);

    const int status = tool.run(newFrontendActionFactory(&finder).get());
    if (status != 0) {
        return status;
    }

    std::sort(cases.begin(), cases.end(),
              [](const TestCaseInfo &lhs, const TestCaseInfo &rhs) { return lhs.display_name < rhs.display_name; });

    return emit(options, cases);
}
