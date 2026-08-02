#include "tooling_support.hpp"

#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/Tooling.h>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace clang::ast_matchers;
using gentest::codegen::SourceDeclMatcher;
using gentest::codegen::SourceTraversalPolicy;

namespace {

struct Run {
    int failures = 0;

    void expect(bool ok, std::string_view message) {
        if (!ok) {
            ++failures;
            std::cerr << "FAIL: " << message << "\n";
        }
    }
};

class DeclCollector final : public MatchFinder::MatchCallback {
  public:
    using NameQuery = std::pair<std::string_view, std::string_view>;

    void run(const MatchFinder::MatchResult &result) override {
        collect<clang::FunctionDecl>(result, "function");
        collect<clang::CXXRecordDecl>(result, "record");
        collect<clang::FunctionTemplateDecl>(result, "function_template");
        collect<clang::ClassTemplateDecl>(result, "class_template");
        collect<clang::TypeAliasDecl>(result, "alias");
        collect<clang::TypeAliasTemplateDecl>(result, "alias_template");
        collect<clang::NamespaceDecl>(result, "namespace");
        collect<clang::VarDecl>(result, "variable");
        collect<clang::TypedefNameDecl>(result, "typedef_name");
    }

    [[nodiscard]] bool contains(NameQuery query) const {
        const auto it = names_.find(std::string{query.first});
        return it != names_.end() && it->second.contains(std::string{query.second});
    }

    [[nodiscard]] bool contains_suffix(NameQuery query) const {
        const auto it = names_.find(std::string{query.first});
        if (it == names_.end()) {
            return false;
        }
        for (const auto &name : it->second) {
            if (name.ends_with(query.second)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t match_count() const { return match_count_; }

  private:
    template <typename DeclT> void collect(const MatchFinder::MatchResult &result, std::string_view binding) {
        if (const auto *decl = result.Nodes.getNodeAs<DeclT>(binding)) {
            names_[std::string{binding}].insert(decl->getQualifiedNameAsString());
            ++match_count_;
        }
    }

    std::map<std::string, std::set<std::string>> names_;
    std::size_t                                  match_count_ = 0;
};

void register_decl_matchers(MatchFinder &finder, DeclCollector &collector) {
    finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("function"), &collector);
    finder.addMatcher(cxxRecordDecl(isDefinition(), unless(isImplicit())).bind("record"), &collector);
    finder.addMatcher(functionTemplateDecl(unless(isImplicit())).bind("function_template"), &collector);
    finder.addMatcher(classTemplateDecl(unless(isImplicit())).bind("class_template"), &collector);
    finder.addMatcher(typeAliasDecl(unless(isImplicit())).bind("alias"), &collector);
    finder.addMatcher(typeAliasTemplateDecl(unless(isImplicit())).bind("alias_template"), &collector);
    finder.addMatcher(namespaceDecl().bind("namespace"), &collector);
    finder.addMatcher(varDecl(unless(isImplicit())).bind("variable"), &collector);
    finder.addMatcher(typedefNameDecl(unless(isImplicit())).bind("typedef_name"), &collector);
}

auto build_ast(std::string_view code, const clang::tooling::FileContentMappings &mappings = {}, std::vector<std::string> extra_args = {})
    -> std::unique_ptr<clang::ASTUnit> {
    std::vector<std::string> args{"-std=c++20"};
    args.insert(args.end(), std::make_move_iterator(extra_args.begin()), std::make_move_iterator(extra_args.end()));
    return clang::tooling::buildASTFromCodeWithArgs(code, args, "/virtual/main.cpp", "clang-tool",
                                                    std::make_shared<clang::PCHContainerOperations>(),
                                                    clang::tooling::getClangStripDependencyFileAdjuster(), mappings);
}

auto collect_declarations(clang::ASTContext &context, SourceTraversalPolicy policy) -> DeclCollector {
    MatchFinder   finder;
    DeclCollector collector;
    register_decl_matchers(finder, collector);

    SourceDeclMatcher matcher{finder, context, policy};
    for (clang::Decl *decl : context.getTranslationUnitDecl()->decls()) {
        matcher.match(decl);
    }
    return collector;
}

} // namespace

int main() {
    Run t;

    {
        auto ast = build_ast(R"cpp(
namespace sample {
void plain() {}
struct Record {};

template <typename T> void functionTemplate() {}
template <typename T> struct ClassTemplate {};
template <typename T> using AliasTemplate = ClassTemplate<T>;
template <typename T> T variableTemplate{};

using PlainAlias = int;
int globalVariable = 0;

void outer() {
    struct LocalRecord {
        void member() {}
    };
    int localVariable = 0;
}
} // namespace sample
)cpp");
        t.expect(ast != nullptr, "representative source parses");
        if (ast != nullptr) {
            MatchFinder   finder;
            DeclCollector collector;
            register_decl_matchers(finder, collector);

            auto             &context = ast->getASTContext();
            SourceDeclMatcher matcher{finder, context, SourceTraversalPolicy{}};
            for (clang::Decl *decl : context.getTranslationUnitDecl()->decls()) {
                matcher.match(decl);
            }

            t.expect(collector.contains({"function", "sample::plain"}), "plain function declaration is matched");
            t.expect(collector.contains({"record", "sample::Record"}), "plain record declaration is matched");
            t.expect(collector.contains({"function_template", "sample::functionTemplate"}), "function template wrapper is matched");
            t.expect(collector.contains({"function", "sample::functionTemplate"}), "function template declaration is unwrapped");
            t.expect(collector.contains({"class_template", "sample::ClassTemplate"}), "class template wrapper is matched");
            t.expect(collector.contains({"record", "sample::ClassTemplate"}), "class template declaration is unwrapped");
            t.expect(collector.contains({"alias_template", "sample::AliasTemplate"}), "alias template wrapper is matched");
            t.expect(collector.contains({"alias", "sample::AliasTemplate"}), "alias template declaration is generically unwrapped");
            t.expect(collector.contains({"variable", "sample::variableTemplate"}),
                     "variable template declaration is generically unwrapped");
            t.expect(collector.contains({"namespace", "sample"}), "dynamic dispatch matches a declaration kind not named by the walker");
            t.expect(collector.contains_suffix({"record", "LocalRecord"}), "local record declaration is reached lexically");
            t.expect(collector.contains_suffix({"function", "LocalRecord::member"}), "member of a local record is reached lexically");
            t.expect(collector.contains_suffix({"variable", "localVariable"}), "local variable declaration is reached lexically");

            const std::size_t first_pass_matches = collector.match_count();
            matcher.match(nullptr);
            for (clang::Decl *decl : context.getTranslationUnitDecl()->decls()) {
                matcher.match(decl);
            }
            t.expect(collector.match_count() == first_pass_matches, "visited declarations are not matched through duplicate paths");
        }
    }

    {
        const clang::tooling::FileContentMappings mappings = {
            {"/virtual/include/textual.hpp", R"cpp(
namespace textual {
struct IncludedRecord {};
using IncludedAlias = int;
void includedFunction() {}
} // namespace textual
)cpp"},
            {"/virtual/system/system.hpp", R"cpp(
namespace dependency {
struct SystemRecord {};
void systemFunction() {}
} // namespace dependency
)cpp"},
        };
        auto ast = build_ast(R"cpp(
#include "textual.hpp"
#include <system.hpp>
void mainFunction() {}
)cpp",
                             mappings, {"-I/virtual/include", "-isystem", "/virtual/system"});
        t.expect(ast != nullptr, "textual and system include source parses");
        if (ast != nullptr) {
            auto &context = ast->getASTContext();

            const auto source_only = collect_declarations(context, SourceTraversalPolicy{});
            t.expect(source_only.contains({"function", "mainFunction"}), "main-file function remains discoverable");
            t.expect(source_only.contains({"record", "textual::IncludedRecord"}),
                     "record traversal through a disallowed textual include preserves existing policy");
            t.expect(!source_only.contains({"function", "textual::includedFunction"}), "disallowed textual include function is rejected");
            t.expect(!source_only.contains({"typedef_name", "textual::IncludedAlias"}), "ordinary include alias is rejected by default");

            const auto mock_includes =
                collect_declarations(context, SourceTraversalPolicy{.allow_includes = false, .allow_mock_includes = true});
            t.expect(mock_includes.contains({"typedef_name", "textual::IncludedAlias"}), "mock include policy accepts textual aliases");
            t.expect(!mock_includes.contains({"function", "textual::includedFunction"}),
                     "mock include policy does not broaden function discovery");

            const auto all_textual =
                collect_declarations(context, SourceTraversalPolicy{.allow_includes = true, .allow_mock_includes = false});
            t.expect(all_textual.contains({"function", "textual::includedFunction"}), "allowed textual include function is matched");
            t.expect(all_textual.contains({"typedef_name", "textual::IncludedAlias"}), "allowed textual include alias is matched");
            t.expect(!all_textual.contains({"record", "dependency::SystemRecord"}), "system header record remains excluded");
            t.expect(!all_textual.contains({"function", "dependency::systemFunction"}), "system header function remains excluded");
        }
    }

    return t.failures == 0 ? 0 : 1;
}
