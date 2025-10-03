// Implementation of AST discovery + validation

#include "discovery.hpp"
#include "parse.hpp"
#include "validate.hpp"

#include <utility>
#include <fmt/core.h>
#include "render.hpp"
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

    // Allow templated functions; instantiation handled by codegen.

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

    // Inline classification to support template/parameter expansion
    const auto  collected = collect_gentest_attributes_for(*func, *sm);
    const auto &parsed    = collected.gentest;
    auto report = [&](std::string_view message) {
        const SourceLocation  sloc    = sm->getSpellingLoc(func->getBeginLoc());
        const llvm::StringRef file    = sm->getFilename(sloc);
        const unsigned        lnum    = sm->getSpellingLineNumber(sloc);
        const std::string     subject = func->getQualifiedNameAsString();
        const std::string     locpfx  = !file.empty() ? fmt::format("{}:{}: ", file.str(), lnum) : std::string{};
        const std::string     subj    = !subject.empty() ? fmt::format(" ({})", subject) : std::string{};
        llvm::errs() << fmt::format("gentest_codegen: {}{}{}\n", locpfx, message, subj);
    };
    for (const auto &message : collected.other_namespaces) {
        report(fmt::format("attribute '{}' ignored (unsupported attribute namespace)", message));
    }
    if (parsed.empty()) return;
    auto summary = validate_attributes(parsed, [&](const std::string &m) { had_error_ = true; report(m); });
    if (!summary.case_name.has_value()) return;
    if (!func->doesThisDeclarationHaveABody()) return;

    std::string qualified = func->getQualifiedNameAsString();
    if (qualified.empty()) qualified = func->getNameAsString();
    if (qualified.find("(anonymous namespace)") != std::string::npos) {
        llvm::errs() << fmt::format("gentest_codegen: ignoring test in anonymous namespace: {}\n", qualified);
        return;
    }
    auto file_loc = sm->getFileLoc(func->getLocation());
    auto filename = sm->getFilename(file_loc);
    if (filename.empty()) return;
    unsigned lnum = sm->getSpellingLineNumber(file_loc);

    // Build type combinations
    std::vector<std::vector<std::string>> type_combos{{}};
    if (!summary.template_sets.empty()) {
        type_combos.clear();
        type_combos.emplace_back();
        for (const auto &pair : summary.template_sets) {
            std::vector<std::vector<std::string>> next;
            for (const auto &acc : type_combos) {
                for (const auto &ty : pair.second) {
                    auto w = acc; w.push_back(ty); next.push_back(std::move(w));
                }
            }
            type_combos = std::move(next);
        }
    }
    if (type_combos.empty()) type_combos.push_back({});

    auto make_qualified = [&](const std::vector<std::string>& tmpl) {
        if (tmpl.empty()) return qualified;
        std::string q = qualified; q += '<';
        for (std::size_t i = 0; i < tmpl.size(); ++i) { if (i) q += ", "; q += tmpl[i]; }
        q += '>';
        return q;
    };
    auto make_display = [&](const std::string& base, const std::vector<std::string>& tmpl, const std::string& call_args) {
        std::string nm = base;
        if (!tmpl.empty()) { nm += '<'; for (std::size_t i=0;i<tmpl.size();++i){ if(i) nm+=","; nm+=tmpl[i]; } nm+='>'; }
        if (!call_args.empty()) { nm += '('; nm += call_args; nm += ')'; }
        return nm;
    };
    auto add_case = [&](const std::vector<std::string>& tmpl, const std::string& call_args){
        TestCaseInfo info{};
        info.qualified_name = make_qualified(tmpl);
        info.display_name   = make_display(*summary.case_name, tmpl, call_args);
        info.filename       = filename.str();
        info.line           = lnum;
        info.tags           = summary.tags;
        info.requirements   = summary.requirements;
        info.should_skip    = summary.should_skip;
        info.skip_reason    = summary.skip_reason;
        info.template_args  = tmpl;
        info.call_arguments = call_args;
        if (const auto *method = llvm::dyn_cast<CXXMethodDecl>(func)) {
            if (const auto *record = method->getParent()) {
                const auto class_attrs = collect_gentest_attributes_for(*record, *sm);
                for (const auto &message : class_attrs.other_namespaces) report(fmt::format("attribute '{}' ignored (unsupported attribute namespace)", message));
                auto fixture_summary = validate_fixture_attributes(class_attrs.gentest, [&](const std::string &m) { had_error_ = true; report(m); });
                info.fixture_qualified_name = record->getQualifiedNameAsString();
                info.fixture_stateful       = fixture_summary.stateful;
            }
        }
        std::string key = info.qualified_name + "#" + info.display_name + "@" + info.filename + ":" + std::to_string(info.line);
        if (seen_.insert(key).second) out_.push_back(std::move(info));
    };

    if (!summary.parameter_sets.empty()) {
        // Build Cartesian product of parameter values across axes
        std::vector<std::vector<std::string>> val_combos{{}};
        for (const auto &ps : summary.parameter_sets) {
            std::vector<std::vector<std::string>> next;
            for (const auto &acc : val_combos) {
                for (const auto &v : ps.values) {
                    auto w = acc; w.push_back(v); next.push_back(std::move(w));
                }
            }
            val_combos = std::move(next);
        }
        // Normalize type names for string-likes
        auto is_string_type = [](std::string type) {
            // Remove spaces and lowercase
            type.erase(std::remove_if(type.begin(), type.end(), [](unsigned char c){ return std::isspace(c); }), type.end());
            std::string lower = type;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
            if (lower.find("string_view") != std::string::npos) return true;
            if (lower.find("string") != std::string::npos) return true;
            if (lower.find("char*") != std::string::npos) return true;
            return false;
        };
        for (const auto &combo : type_combos) {
            for (const auto &vals : val_combos) {
                std::string call;
                for (std::size_t i = 0; i < vals.size(); ++i) {
                    if (i) call += ", ";
                    const auto &ps = summary.parameter_sets[i];
                    if (is_string_type(ps.type_name)) {
                        call += '"';
                        call += render::escape_string(vals[i]);
                        call += '"';
                    } else {
                        call += vals[i];
                    }
                }
                add_case(combo, call);
            }
        }
    } else {
        for (const auto &combo : type_combos) add_case(combo, "");
    }
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
        const std::string     locpfx  = !file.empty() ? fmt::format("{}:{}: ", file.str(), line) : std::string{};
        const std::string     subj    = !subject.empty() ? fmt::format(" ({})", subject) : std::string{};
        llvm::errs() << fmt::format("gentest_codegen: {}{}{}\n", locpfx, message, subj);
    };

    for (const auto &message : collected.other_namespaces) {
        report(fmt::format("attribute '{}' ignored (unsupported attribute namespace)", message));
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
        llvm::errs() << fmt::format("gentest_codegen: ignoring test in anonymous namespace: {}\n", qualified);
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
                report(fmt::format("attribute '{}' ignored (unsupported attribute namespace)", message));
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
