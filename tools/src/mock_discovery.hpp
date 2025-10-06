#pragma once

#include "model.hpp"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <llvm/ADT/DenseSet.h>
#include <string_view>
#include <vector>

namespace gentest::codegen {

class MockUsageCollector : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    explicit MockUsageCollector(std::vector<MockClassInfo> &out);

    void run(const clang::ast_matchers::MatchFinder::MatchResult &result) override;

    [[nodiscard]] bool has_errors() const;

  private:
    void report(const clang::SourceManager &sm, clang::SourceLocation loc, std::string_view message) const;
    void handle_specialization(const clang::ClassTemplateSpecializationDecl        &decl,
                               const clang::ast_matchers::MatchFinder::MatchResult &result);

    std::vector<MockClassInfo>                  &out_;
    llvm::DenseSet<const clang::CXXRecordDecl *> seen_;
    mutable bool                                 had_error_ = false;
};

void register_mock_matchers(clang::ast_matchers::MatchFinder &finder, MockUsageCollector &collector);

} // namespace gentest::codegen
