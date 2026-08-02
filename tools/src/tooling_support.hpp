// Platform/tooling support helpers for clang-tooling invocation.
#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <llvm/ADT/SmallPtrSet.h>

namespace gentest::codegen {

struct SourceTraversalPolicy {
    bool allow_includes      = false;
    bool allow_mock_includes = false;

    [[nodiscard]] bool accepts(const clang::Decl &decl, const clang::SourceManager &source_manager) const;
};

// Matches declarations lexically owned by the source being scanned without
// recursively traversing declarations deserialized from imported AST files.
class SourceDeclMatcher final {
  public:
    SourceDeclMatcher(clang::ast_matchers::MatchFinder &finder, clang::ASTContext &context, SourceTraversalPolicy policy);

    void match(clang::Decl *decl);

  private:
    clang::ast_matchers::MatchFinder          &finder_;
    clang::ASTContext                         &context_;
    SourceTraversalPolicy                      policy_;
    llvm::SmallPtrSet<const clang::Decl *, 32> visited_;
};

} // namespace gentest::codegen
