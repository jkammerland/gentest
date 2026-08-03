// Implementation of platform/tooling helpers

#include "tooling_support.hpp"

#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/Support/Casting.h>

namespace gentest::codegen {

bool SourceTraversalPolicy::accepts(const clang::Decl &decl, const clang::SourceManager &source_manager) const {
    clang::SourceLocation loc = decl.getBeginLoc();
    if (loc.isInvalid()) {
        loc = decl.getLocation();
    }
    if (loc.isInvalid()) {
        return false;
    }
    if (loc.isMacroID()) {
        loc = source_manager.getExpansionLoc(loc);
    }
    if (loc.isInvalid()) {
        return false;
    }

    // Imports remain available to semantic analysis, but their declarations
    // belong to the provider's codegen invocation rather than this source.
    if (decl.isFromASTFile() || decl.isInAnotherModuleUnit()) {
        return false;
    }
    if (source_manager.isInSystemHeader(loc) || source_manager.isWrittenInBuiltinFile(loc)) {
        return false;
    }
    if (source_manager.isWrittenInMainFile(loc)) {
        return true;
    }
    if (!allow_includes) {
        if (llvm::isa<clang::NamespaceDecl>(decl) || llvm::isa<clang::CXXRecordDecl>(decl)) {
            return true;
        }
        if (allow_mock_includes && llvm::isa<clang::TypedefNameDecl>(decl)) {
            return true;
        }
        return false;
    }
    return true;
}

SourceDeclMatcher::SourceDeclMatcher(clang::ast_matchers::MatchFinder &finder, clang::ASTContext &context, SourceTraversalPolicy policy)
    : finder_(finder), context_(context), policy_(policy) {}

void SourceDeclMatcher::match(clang::Decl *decl) {
    if (decl == nullptr || !visited_.insert(decl).second) {
        return;
    }
    if (!policy_.accepts(*decl, context_.getSourceManager())) {
        return;
    }

    // DynTypedNode preserves the declaration's exact runtime kind. Matcher
    // registration therefore remains the only list of supported declarations.
    finder_.match(clang::DynTypedNode::create(*decl), context_);

    // TemplateDecl covers function, class, alias, variable, and future
    // declaration-template kinds without duplicating that list here.
    if (const auto *template_decl = llvm::dyn_cast<clang::TemplateDecl>(decl)) {
        match(template_decl->getTemplatedDecl());
    }

    if (const auto *decl_context = llvm::dyn_cast<clang::DeclContext>(decl)) {
        for (clang::Decl *child : decl_context->decls()) {
            match(child);
        }
    }
}

} // namespace gentest::codegen
