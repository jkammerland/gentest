#include "mock_type_printer.hpp"

#include <clang/AST/Decl.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Tooling/Tooling.h>
#include <iostream>
#include <string>

int main() {
    // Keep the input independent of system headers and valid for every host ABI.
    const auto ast = clang::tooling::buildASTFromCodeWithArgs(R"cpp(
        namespace detail { struct Handle { int id; }; }
        template <class T> struct Box {};
        void pointers(int detail::Handle::*, int detail::Handle::* const,
                      int (detail::Handle::*)(double) const,
                      Box<int detail::Handle::*>);
    )cpp",
                                                              {"-std=c++20"});
    if (!ast || ast->getDiagnostics().hasErrorOccurred()) {
        std::cerr << "FAIL: cannot parse member pointer fixture\n";
        return 1;
    }

    const clang::FunctionDecl *function = nullptr;
    for (const auto *decl : ast->getASTContext().getTranslationUnitDecl()->decls()) {
        if (const auto *candidate = llvm::dyn_cast<clang::FunctionDecl>(decl); candidate && candidate->getName() == "pointers") {
            function = candidate;
        }
    }
    if (!function || function->getNumParams() != 4) {
        std::cerr << "FAIL: missing member pointer parameters\n";
        return 1;
    }

    // Exact spellings guard MSVC's parsing ambiguity; named and abstract
    // declarators must both retain qualification and member/function qualifiers.
    const std::string expected[] = {"int (::detail::Handle::*)", "int (::detail::Handle::*const)",
                                    "int (::detail::Handle::*)(double) const", "::Box<int (::detail::Handle::*)>"};
    const std::string named[]    = {"int (::detail::Handle::*value)", "int (::detail::Handle::*const value)",
                                    "int (::detail::Handle::*value)(double) const", "::Box<int (::detail::Handle::*)> value"};
    int               failures   = 0;
    for (unsigned i = 0; i < function->getNumParams(); ++i) {
        for (const auto spelling : {gentest::codegen::MockTypeSpelling::Canonical, gentest::codegen::MockTypeSpelling::AsWritten}) {
            const auto type       = function->getParamDecl(i)->getType();
            const auto abstract   = gentest::codegen::print_mock_type(type, ast->getASTContext(), spelling);
            const auto declarator = gentest::codegen::print_mock_type(type, ast->getASTContext(), spelling, "value");
            if (abstract != expected[i] || declarator != named[i]) {
                std::cerr << "FAIL: parameter " << i << ": " << abstract << " / " << declarator << '\n';
                ++failures;
            }
        }
    }
    return failures == 0 ? 0 : 1;
}
