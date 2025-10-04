// Implementation of AST discovery + validation

#include "discovery.hpp"
#include "parse.hpp"
#include "validate.hpp"

#include <algorithm>
#include <utility>
#include <fmt/core.h>
#include "render.hpp"
#include "type_kind.hpp"
#include "axis_expander.hpp"
#include "discovery_utils.hpp"
#include <clang/AST/Decl.h>
#include <clang/Basic/SourceManager.h>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <set>
#include <string>

using namespace clang;
using namespace clang::ast_matchers;
using gentest::codegen::TypeKind;
using gentest::codegen::classify_type;
using gentest::codegen::quote_for_type;

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

    // 'fixtures(...)' applies only to free functions; reject on member functions.
    if (!summary.fixtures_types.empty()) {
        if (llvm::isa<CXXMethodDecl>(func)) {
            had_error_ = true;
            report("'fixtures(...)' is not supported on member tests");
            return;
        }
    }

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

    // Validate template attribute usage and collect declaration order (optional under flag).
    std::vector<disc::TParam> fn_params_order;
    if (!summary.template_sets.empty() || !summary.template_nttp_sets.empty()) {
        if (!disc::collect_template_params(*func, fn_params_order)) {
#ifndef GENTEST_DISABLE_TEMPLATE_VALIDATION
            had_error_ = true; report("'template(...)' attributes present but function is not a template"); return;
#else
            fn_params_order.clear(); // fallback to attribute order later
#endif
        }
#ifndef GENTEST_DISABLE_TEMPLATE_VALIDATION
        if (!fn_params_order.empty()) {
            if (!disc::validate_template_attributes(summary.template_sets, summary.template_nttp_sets, fn_params_order,
                                                    [&](const std::string& m){ had_error_ = true; report(m); })) {
                return;
            }
        }
#endif
    }

    // Build combined template argument combinations
    std::vector<std::vector<std::string>> combined_tpl_combos;
    if (!summary.template_sets.empty() || !summary.template_nttp_sets.empty()) {
#ifndef GENTEST_DISABLE_TEMPLATE_VALIDATION
        if (!fn_params_order.empty()) {
            combined_tpl_combos = disc::build_template_arg_combos(summary.template_sets, summary.template_nttp_sets, fn_params_order);
        } else
#endif
        {
            combined_tpl_combos = disc::build_template_arg_combos_attr_order(summary.template_sets, summary.template_nttp_sets);
        }
    }
    if (combined_tpl_combos.empty()) combined_tpl_combos.push_back({});

    auto make_qualified = [&](const std::vector<std::string>& tpl_ordered) {
        if (tpl_ordered.empty()) return qualified;
        std::string q = qualified; q += '<';
        for (std::size_t i = 0; i < tpl_ordered.size(); ++i) { if (i) q += ", "; q += tpl_ordered[i]; }
        q += '>';
        return q;
    };
    auto make_display = [&](const std::string& base, const std::vector<std::string>& tpl_ordered, const std::string& call_args) {
        std::string nm = base;
        if (!tpl_ordered.empty()) {
            nm += '<';
            for (std::size_t i=0;i<tpl_ordered.size();++i){ if(i) nm+=","; nm+=tpl_ordered[i]; }
            nm+='>';
        }
        if (!call_args.empty()) { nm += '('; nm += call_args; nm += ')'; }
        return nm;
    };
    // Determine the enclosing scope for qualifying unqualified fixture types
    std::string enclosing_scope;
    if (auto p = qualified.rfind("::"); p != std::string::npos) enclosing_scope = qualified.substr(0, p);

    auto add_case = [&](const std::vector<std::string>& tpl_ordered, const std::string& call_args){
        TestCaseInfo info{};
        info.qualified_name = make_qualified(tpl_ordered);
        info.display_name   = make_display(*summary.case_name, tpl_ordered, call_args);
        info.filename       = filename.str();
        info.line           = lnum;
        info.tags           = summary.tags;
        info.requirements   = summary.requirements;
        info.should_skip    = summary.should_skip;
        info.skip_reason    = summary.skip_reason;
        info.template_args  = tpl_ordered;
        info.call_arguments = call_args;
        // Qualify fixture type names if unqualified, using the function's enclosing scope
        if (!summary.fixtures_types.empty()) {
            for (const auto &ty : summary.fixtures_types) {
                if (ty.find("::") != std::string::npos) info.free_fixtures.push_back(ty);
                else if (!enclosing_scope.empty()) info.free_fixtures.push_back(enclosing_scope + "::" + ty);
                else info.free_fixtures.push_back(ty);
            }
        }
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

    if (!summary.parameter_sets.empty() || !summary.param_packs.empty()) {
        // Build Cartesian product of parameter values across axes
        std::vector<std::vector<std::string>> axes_vals; axes_vals.reserve(summary.parameter_sets.size());
        for (const auto& ps : summary.parameter_sets) axes_vals.push_back(ps.values);
        std::vector<std::vector<std::string>> val_combos = gentest::codegen::util::cartesian(axes_vals);
        // Track scalar types in order
        std::vector<std::string> scalar_types;
        for (const auto &ps : summary.parameter_sets) scalar_types.push_back(ps.type_name);
        // Build pack combos: each element carries concatenated args and types
        using ArgTypes = std::pair<std::vector<std::string>, std::vector<std::string>>;
        std::vector<ArgTypes> pack_combos{{}}; // start with empty (args, types)
        for (const auto &pp : summary.param_packs) {
            std::vector<ArgTypes> next;
            for (const auto &partial : pack_combos) {
                for (const auto &row : pp.rows) {
                    ArgTypes nt = partial;
                    nt.first.insert(nt.first.end(), row.begin(), row.end());
                    nt.second.insert(nt.second.end(), pp.types.begin(), pp.types.end());
                    next.push_back(std::move(nt));
                }
            }
            pack_combos = std::move(next);
        }
        if (pack_combos.empty()) pack_combos.push_back({{}, {}});
        // No expansion guardrails: generate all combinations as requested by attributes.

        for (const auto &tpl_combo : combined_tpl_combos) {
            for (const auto &pack : pack_combos) {
                for (const auto &vals : val_combos) {
                    std::string call;
                    std::vector<std::string> types_concat = pack.second; // pack types
                    types_concat.insert(types_concat.end(), scalar_types.begin(), scalar_types.end());
                    std::vector<std::string> args_concat = pack.first; // pack args
                    args_concat.insert(args_concat.end(), vals.begin(), vals.end());
                    for (std::size_t i = 0; i < args_concat.size(); ++i) {
                        if (i) call += ", ";
                        const auto &ty = types_concat[i];
                        auto kind = classify_type(ty);
                        call += quote_for_type(kind, args_concat[i], ty);
                    }
                    add_case(tpl_combo, call);
                }
            }
        }
    } else {
        for (const auto &tpl_combo : combined_tpl_combos) add_case(tpl_combo, "");
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
