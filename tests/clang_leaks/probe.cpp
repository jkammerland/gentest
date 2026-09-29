#include <array>
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/DependentDiagnostic.h>
#include <clang/Basic/PartialDiagnostic.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Sema/Sema.h>
#include <clang/Sema/TemplateDeduction.h>
#include <clang/Tooling/Tooling.h>
#include <iostream>
#include <string>
#include <string_view>

static int runProbe(std::string_view kind, bool control) {
    auto ast = clang::tooling::buildASTFromCodeWithArgs("template<class T> struct Probe {};", {"-std=c++20"});
    if (!ast)
        return 2;
    auto &context = ast->getASTContext();
    if (kind == "dependent") {
        for (auto *decl : context.getTranslationUnitDecl()->decls()) {
            auto *templateDecl = llvm::dyn_cast<clang::ClassTemplateDecl>(decl);
            if (!templateDecl)
                continue;
            auto                    *record = templateDecl->getTemplatedDecl();
            clang::PartialDiagnostic diagnostic(0, context.getDiagAllocator());
            diagnostic << std::string(30, 'a') << std::string(31, 'b') << std::string(31, 'c');
            if (!control)
                clang::DependentDiagnostic::Create(context, record, clang::DependentDiagnostic::Access, {}, true, clang::AS_private, record,
                                                   record, {}, diagnostic);
            return 0;
        }
        return 3;
    }
    if (kind == "deduction") {
        // Exceed ConstraintSatisfaction's inline TemplateArgs capacity. Both
        // paths destroy the original; only the candidate creates the arena copy.
        std::array<clang::TemplateArgument, 8> arguments;
        arguments.fill(clang::TemplateArgument(context.IntTy));
        clang::sema::TemplateDeductionInfo info({});
        info.AssociatedConstraintsSatisfaction = clang::ConstraintSatisfaction(nullptr, arguments);
        if (!control) {
            auto failure = clang::MakeDeductionFailureInfo(context, clang::TemplateDeductionResult::ConstraintsNotSatisfied, info);
            failure.Destroy();
        }
        return 0;
    }
    return 4;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3 || (argc == 3 && std::string_view(argv[2]) != "control"))
        return 5;
    const int result = runProbe(argv[1], argc == 3);
    if (result != 0)
        return result;
    // Emitted only after the original diagnostic and AST context were destroyed.
    std::cout << "PROBE_COMPLETED " << argv[1] << (argc == 3 ? " control" : " candidate") << std::endl;
    return 0;
}
