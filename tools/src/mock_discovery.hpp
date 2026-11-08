#pragma once

#include "model.hpp"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <llvm/ADT/DenseMap.h>
#include <string_view>
#include <vector>

namespace clang {
class Sema;
} // namespace clang

namespace gentest::codegen {

class MockUsageCollector : public clang::ast_matchers::MatchFinder::MatchCallback {
  public:
    explicit MockUsageCollector(std::vector<MockClassInfo> &out);

    void                                run(const clang::ast_matchers::MatchFinder::MatchResult &result) override;
    std::optional<clang::TraversalKind> getCheckTraversalKind() const override { return clang::TK_IgnoreUnlessSpelledInSource; }

    [[nodiscard]] bool has_errors() const;

    // Sema of the translation unit being matched, or nullptr. When set, mock
    // targets that are class template specializations are instantiated and
    // deferred exception specifications are resolved before capture.
    void set_sema(clang::Sema *sema) { sema_ = sema; }

  private:
    void report(const clang::SourceManager &sm, clang::SourceLocation loc, std::string_view message) const;
    void handle_mock_target_type(const clang::QualType &target_type, clang::SourceLocation use_loc,
                                 const clang::ast_matchers::MatchFinder::MatchResult &result);
    void handle_specialization(const clang::ClassTemplateSpecializationDecl        &decl,
                               const clang::ast_matchers::MatchFinder::MatchResult &result);
    void handle_typedef(const clang::TypedefNameDecl &decl, const clang::ast_matchers::MatchFinder::MatchResult &result);

    std::vector<MockClassInfo>                               &out_;
    llvm::DenseMap<const clang::CXXRecordDecl *, std::size_t> seen_;
    mutable bool                                              had_error_ = false;
    clang::Sema                                              *sema_      = nullptr;
};

void register_mock_matchers(clang::ast_matchers::MatchFinder &finder, MockUsageCollector &collector);

} // namespace gentest::codegen
