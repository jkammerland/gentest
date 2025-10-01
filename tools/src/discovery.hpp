// Discovery using clang AST and attribute validation
#pragma once

#include "model.hpp"

#include <clang/ASTMatchers/ASTMatchFinder.h>

namespace gentest::codegen {

class TestCaseCollector : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    explicit TestCaseCollector(std::vector<TestCaseInfo> &out);

    void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override;

    [[nodiscard]] bool has_errors() const;

  private:
    std::optional<TestCaseInfo> classify(const clang::FunctionDecl &func, const clang::SourceManager &sm,
                                         const clang::LangOptions &lang) const;

    std::vector<TestCaseInfo>                                         &out_;
    std::set<std::pair<std::string, std::pair<std::string, unsigned>>> seen_;
    mutable bool                                                       had_error_ = false;
};

} // namespace gentest::codegen

