// Discovery using clang AST and attribute validation.
#pragma once

#include "model.hpp"

#include <clang/ASTMatchers/ASTMatchFinder.h>

namespace gentest::codegen {

// AST matcher callback that classifies functions as tests based on parsed
// attributes and validation rules.
class TestCaseCollector : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    // out: vector to append discovered tests to.
    explicit TestCaseCollector(std::vector<TestCaseInfo> &out);

    // Called by clang tooling; extracts a TestCaseInfo when the bound node is a function definition.
    void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override;

    // Whether any hard validation errors were observed.
    [[nodiscard]] bool has_errors() const;

  private:
    // Convert a FunctionDecl into a TestCaseInfo if it has gentest attributes and a function body.
    std::optional<TestCaseInfo> classify(const clang::FunctionDecl &func, const clang::SourceManager &sm,
                                         const clang::LangOptions &lang) const;

    std::vector<TestCaseInfo>                                         &out_;
    std::set<std::pair<std::string, std::pair<std::string, unsigned>>> seen_;
    mutable bool                                                       had_error_ = false;
};

} // namespace gentest::codegen
