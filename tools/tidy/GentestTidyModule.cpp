#include "parse.hpp"
#include "validate.hpp"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Tooling/Tooling.h>
#include <clang-tidy/ClangTidy.h>
#include <clang-tidy/ClangTidyCheck.h>
#include <clang-tidy/ClangTidyModule.h>
#include <clang-tidy/ClangTidyModuleRegistry.h>

using namespace clang;
using namespace clang::ast_matchers;

namespace gentest::tidy {

class GentestAttributesCheck : public clang::tidy::ClangTidyCheck {
  public:
    GentestAttributesCheck(llvm::StringRef Name, clang::tidy::ClangTidyContext *Context)
        : clang::tidy::ClangTidyCheck(Name, Context) {}

    void registerMatchers(MatchFinder *Finder) override {
        Finder->addMatcher(functionDecl(isDefinition()).bind("func"), this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("func");
        if (!FD) return;
        const auto *SM = Result.SourceManager;

        const auto collected = gentest::codegen::collect_gentest_attributes_for(*FD, *SM);

        for (const auto &message : collected.other_namespaces) {
            diag(FD->getBeginLoc(), "attribute '%0' ignored (unsupported attribute namespace)") << message;
        }

        if (collected.gentest.empty()) return;

        auto summary = gentest::codegen::validate_attributes(collected.gentest, [&](const std::string &m) {
            diag(FD->getBeginLoc(), m);
        });
        (void)summary;
    }
};

class GentestTidyModule : public clang::tidy::ClangTidyModule {
  public:
    void addCheckFactories(clang::tidy::ClangTidyCheckFactories &Factories) override {
        Factories.registerCheck<GentestAttributesCheck>("gentest-attributes");
    }
};

} // namespace gentest::tidy

static clang::tidy::ClangTidyModuleRegistry::Add<gentest::tidy::GentestTidyModule>
    X("gentest-module", "Gentest attributes validation checks");

// This anchor is used to force the linker to link in the generated object file
// and thus register the module.
volatile int GentestTidyModuleAnchorSource = 0;

