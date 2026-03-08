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
        Finder->addMatcher(cxxRecordDecl(isDefinition(), unless(isImplicit())).bind("record"), this);
        Finder->addMatcher(namespaceDecl().bind("ns"), this);
    }

    void check(const MatchFinder::MatchResult &Result) override {
        const auto *SM = Result.SourceManager;
        if (!SM) return;

        auto emit_collection_diags = [&](SourceLocation loc, const gentest::codegen::AttributeCollection &collected) {
            for (const auto &message : collected.mis_scoped_gentest) {
                diag(loc, "attribute '%0' must use '[[using gentest: ...]]' or explicit 'gentest::' qualification") << message;
            }

            for (const auto &message : collected.other_namespaces) {
                diag(loc, "attribute '%0' ignored (unsupported attribute namespace)") << message;
            }
        };

        if (const auto *FD = Result.Nodes.getNodeAs<FunctionDecl>("func")) {
            const auto collected = gentest::codegen::collect_gentest_attributes_for(*FD, *SM);
            emit_collection_diags(FD->getBeginLoc(), collected);
            if (collected.gentest.empty()) return;

            auto summary = gentest::codegen::validate_attributes(collected.gentest, [&](const std::string &m) {
                diag(FD->getBeginLoc(), m);
            });
            (void)summary;
            return;
        }

        if (const auto *RD = Result.Nodes.getNodeAs<CXXRecordDecl>("record")) {
            const auto collected = gentest::codegen::collect_gentest_attributes_for(*RD, *SM);
            emit_collection_diags(RD->getBeginLoc(), collected);
            if (collected.gentest.empty()) return;

            auto summary = gentest::codegen::validate_fixture_attributes(collected.gentest, [&](const std::string &m) {
                diag(RD->getBeginLoc(), m);
            });
            (void)summary;
            return;
        }

        if (const auto *NS = Result.Nodes.getNodeAs<NamespaceDecl>("ns")) {
            SourceLocation loc = NS->getBeginLoc();
            if (loc.isInvalid()) loc = NS->getLocation();

            const auto collected = gentest::codegen::collect_gentest_attributes_for(*NS, *SM);
            emit_collection_diags(loc, collected);
            if (collected.gentest.empty()) return;

            auto summary = gentest::codegen::validate_namespace_attributes(collected.gentest, [&](const std::string &m) {
                diag(loc, m);
            });
            (void)summary;
        }
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
