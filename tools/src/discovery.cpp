// Implementation of AST discovery + validation

#include "discovery.hpp"
#include "parse.hpp"
#include "validate.hpp"

#include <utility>
#include <clang/AST/Decl.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <set>
#include <string>

using namespace clang;
using namespace clang::ast_matchers;

namespace gentest::codegen {

TestCaseCollector::TestCaseCollector(std::vector<TestCaseInfo> &out) : out_(out) {}

void TestCaseCollector::run(const MatchFinder::MatchResult &result) {
    const auto *func = result.Nodes.getNodeAs<FunctionDecl>("gentest.func");
    if (func == nullptr) {
        return;
    }

    const auto *sm   = result.SourceManager;
    const auto &lang = result.Context->getLangOpts();

    if (func->isTemplated() || func->isDependentContext()) {
        return;
    }

    auto loc = func->getBeginLoc();
    if (loc.isInvalid()) {
        return;
    }
    if (loc.isMacroID()) {
        loc = sm->getExpansionLoc(loc);
    }

    if (!sm->isWrittenInMainFile(loc)) {
        return;
    }

    if (sm->isInSystemHeader(loc) || sm->isWrittenInBuiltinFile(loc)) {
        return;
    }

    std::optional<TestCaseInfo> info = classify(*func, *sm, lang);
    if (!info.has_value()) {
        return;
    }

    auto key = std::make_pair(info->qualified_name, std::make_pair(info->filename, info->line));
    if (!seen_.insert(std::move(key)).second) {
        return;
    }

    out_.push_back(std::move(info.value()));
}

std::optional<TestCaseInfo> TestCaseCollector::classify(const FunctionDecl &func, const SourceManager &sm,
                                                        const LangOptions &lang) const {
    (void)lang;

    const auto  collected = collect_gentest_attributes_for(func, sm);
    const auto &parsed    = collected.gentest;

    auto report = [&](std::string_view message) {
        const SourceLocation  loc     = sm.getSpellingLoc(func.getBeginLoc());
        const llvm::StringRef file    = sm.getFilename(loc);
        const unsigned        line    = sm.getSpellingLineNumber(loc);
        const std::string     subject = func.getQualifiedNameAsString();
        if (!file.empty()) {
            llvm::errs() << "gentest_codegen: " << file << ':' << line << ": " << message;
        } else {
            llvm::errs() << "gentest_codegen: " << message;
        }
        if (!subject.empty()) {
            llvm::errs() << " (" << subject << ')';
        }
        llvm::errs() << '\n';
    };

    for (const auto &message : collected.other_namespaces) {
        std::string text = "attribute '" + message + "' ignored (unsupported attribute namespace)";
        report(text);
    }

    if (parsed.empty()) {
        return std::nullopt;
    }

    auto summary = validate_attributes(parsed, [&](const std::string &m) {
        had_error_ = true;
        report(m);
    });

    if (!summary.case_name.has_value()) {
        return std::nullopt;
    }

    if (!func.doesThisDeclarationHaveABody()) {
        return std::nullopt;
    }

    std::string qualified = func.getQualifiedNameAsString();
    if (qualified.empty()) {
        qualified = func.getNameAsString();
    }
    if (qualified.find("(anonymous namespace)") != std::string::npos) {
        llvm::errs() << "gentest_codegen: ignoring test in anonymous namespace: " << qualified << "\n";
        return std::nullopt;
    }

    auto file_loc = sm.getFileLoc(func.getLocation());
    auto filename = sm.getFilename(file_loc);
    if (filename.empty()) {
        return std::nullopt;
    }

    unsigned line = sm.getSpellingLineNumber(file_loc);

    TestCaseInfo info{};
    info.qualified_name = std::move(qualified);
    info.display_name   = std::move(*summary.case_name);
    info.filename       = filename.str();
    info.line           = line;
    info.tags           = std::move(summary.tags);
    info.requirements   = std::move(summary.requirements);
    info.should_skip    = summary.should_skip;
    info.skip_reason    = std::move(summary.skip_reason);

    // If this is a method, collect fixture attributes from the parent class/struct.
    if (const auto *method = llvm::dyn_cast<CXXMethodDecl>(&func)) {
        if (const auto *record = method->getParent()) {
            const auto class_attrs = collect_gentest_attributes_for(*record, sm);
            for (const auto &message : class_attrs.other_namespaces) {
                std::string text = "attribute '" + message + "' ignored (unsupported attribute namespace)";
                report(text);
            }
            auto fixture_summary = validate_fixture_attributes(class_attrs.gentest, [&](const std::string &m) {
                had_error_ = true;
                report(m);
            });
            info.fixture_qualified_name = record->getQualifiedNameAsString();
            info.fixture_stateful       = fixture_summary.stateful;
        }
    }
    return info;
}

bool TestCaseCollector::has_errors() const { return had_error_; }

} // namespace gentest::codegen
