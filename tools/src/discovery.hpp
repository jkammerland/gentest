// Discovery using clang AST and attribute validation.
#pragma once

#include "model.hpp"
#include "validate.hpp"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <set>
#include <unordered_map>

namespace gentest::codegen {

// AST matcher callback that classifies functions as tests based on parsed
// attributes and validation rules.
class TestCaseCollector : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    // out: vector to append discovered tests to.
    explicit TestCaseCollector(std::vector<TestCaseInfo> &out, bool strict_fixture, bool allow_includes);

    // Called by clang tooling; extracts a TestCaseInfo when the bound node is a function definition.
    void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override;

    // Whether any hard validation errors were observed.
    [[nodiscard]] bool has_errors() const;

  private:
    // Convert a FunctionDecl into a TestCaseInfo if it has gentest attributes and a function body.
    std::optional<TestCaseInfo> classify(const clang::FunctionDecl &func, const clang::SourceManager &sm,
                                         const clang::LangOptions &lang) const;
    void                        report(const clang::FunctionDecl &func, const clang::SourceManager &sm, std::string_view message) const;

    std::vector<TestCaseInfo> &out_;
    bool                       strict_fixture_ = false;
    bool                       allow_includes_ = false;
    // Dedup emitted test cases by a composite key (qualified + display + file:line)
    std::set<std::string>                                                           seen_;
    mutable bool                                                                    had_error_ = false;
    mutable std::unordered_map<const clang::NamespaceDecl *, SuiteAttributeSummary> suite_cache_;
    // Enforce unique final base names (suite_path/base), before decorations
    mutable std::unordered_map<std::string, std::string> unique_base_locations_;
};

} // namespace gentest::codegen
