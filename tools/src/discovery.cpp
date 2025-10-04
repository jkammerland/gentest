// Implementation of AST discovery + validation

#include "discovery.hpp"
#include "parse.hpp"
#include "validate.hpp"

#include <algorithm>
#include <utility>
#include <fmt/core.h>
#include "render.hpp"
#include "type_kind.hpp"
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

    // Validate template attribute usage against the function's template parameter list.
    // We allow arbitrary interleaving of type and NTTP parameters; we require a corresponding
    // attribute set for each parameter by name and kind.
    struct TParam { enum Kind { Type, NTTP } kind; std::string name; };
    std::vector<TParam> fn_params_order;
    if (!summary.template_sets.empty() || !summary.template_nttp_sets.empty()) {
        const FunctionTemplateDecl *ftd = func->getDescribedFunctionTemplate();
        if (ftd == nullptr) {
            had_error_ = true;
            report("'template(...)' attributes present but function is not a template");
            return;
        }
        const TemplateParameterList *tpl = ftd->getTemplateParameters();
        for (unsigned i = 0; i < tpl->size(); ++i) {
            const NamedDecl *p = tpl->getParam(i);
            if (llvm::isa<TemplateTypeParmDecl>(p)) {
                fn_params_order.push_back({TParam::Type, p->getNameAsString()});
            } else if (llvm::isa<NonTypeTemplateParmDecl>(p)) {
                fn_params_order.push_back({TParam::NTTP, p->getNameAsString()});
            } else {
                had_error_ = true;
                report("template-template parameters are not supported");
                return;
            }
        }
        // Build lookup maps
        std::map<std::string, std::vector<std::string>> type_map;
        for (const auto &p : summary.template_sets) type_map.emplace(p.first, p.second);
        std::map<std::string, std::vector<std::string>> nttp_map;
        for (const auto &p : summary.template_nttp_sets) nttp_map.emplace(p.first, p.second);
        // Verify each declared parameter has a corresponding attribute set of the correct kind
        for (const auto &tp : fn_params_order) {
            if (tp.kind == TParam::Type) {
                if (!type_map.contains(tp.name)) {
                    had_error_ = true;
                    report(fmt::format("missing 'template({0}, ...)' attribute for type parameter '{0}'", tp.name));
                    return;
                }
            } else {
                if (!nttp_map.contains(tp.name)) {
                    had_error_ = true;
                    report(fmt::format("missing 'template(NTTP: {0}, ...)' attribute for non-type parameter '{0}'", tp.name));
                    return;
                }
            }
        }
        // Unknown names present in attributes
        for (const auto &kv : type_map) {
            bool known = false; for (const auto &tp : fn_params_order) if (tp.kind==TParam::Type && tp.name==kv.first) { known=true; break; }
            if (!known) { had_error_ = true; report(fmt::format("unknown type template parameter '{}' in attributes", kv.first)); return; }
        }
        for (const auto &kv : nttp_map) {
            bool known = false; for (const auto &tp : fn_params_order) if (tp.kind==TParam::NTTP && tp.name==kv.first) { known=true; break; }
            if (!known) { had_error_ = true; report(fmt::format("unknown NTTP template parameter '{}' in attributes", kv.first)); return; }
        }
    }

    // Build combined template argument combinations in declaration order
    std::vector<std::vector<std::string>> combined_tpl_combos{{}};
    if (!fn_params_order.empty()) {
        // Reuse maps for values
        std::map<std::string, std::vector<std::string>> type_map;
        for (const auto &p : summary.template_sets) type_map.emplace(p.first, p.second);
        std::map<std::string, std::vector<std::string>> nttp_map;
        for (const auto &p : summary.template_nttp_sets) nttp_map.emplace(p.first, p.second);
        combined_tpl_combos.clear();
        combined_tpl_combos.emplace_back();
        for (const auto &tp : fn_params_order) {
            const auto &vals = (tp.kind == TParam::Type) ? type_map[tp.name] : nttp_map[tp.name];
            std::vector<std::vector<std::string>> next;
            for (const auto &acc : combined_tpl_combos) {
                for (const auto &v : vals) { auto w = acc; w.push_back(v); next.push_back(std::move(w)); }
            }
            combined_tpl_combos = std::move(next);
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
        // Normalize type names for string-likes
        auto is_string_type = [](std::string type) {
            // Remove spaces and lowercase
            type.erase(std::remove_if(type.begin(), type.end(), [](unsigned char c){ return std::isspace(c); }), type.end());
            std::string lower = type;
            std::ranges::transform(lower, lower.begin(), [](unsigned char c){ return std::tolower(c); });
            if (lower.find("string_view") != std::string::npos) return true;
            if (lower.find("string") != std::string::npos) return true;
            if (lower.find("char*") != std::string::npos) return true;
            if (lower.find("wchar_t*") != std::string::npos) return true;
            if (lower.find("u8string") != std::string::npos || lower.find("char8_t*") != std::string::npos) return true;
            if (lower.find("u16string") != std::string::npos || lower.find("char16_t*") != std::string::npos) return true;
            if (lower.find("u32string") != std::string::npos || lower.find("char32_t*") != std::string::npos) return true;
            return false;
        };
        auto is_char_type = [](std::string type) {
            type.erase(std::remove_if(type.begin(), type.end(), [](unsigned char c){ return std::isspace(c); }), type.end());
            std::string lower = type;
            std::ranges::transform(lower, lower.begin(), [](unsigned char c){ return std::tolower(c); });
            return lower == "char" || lower == "wchar_t" || lower == "char8_t" || lower == "char16_t" || lower == "char32_t";
        };
        auto string_prefix = [](const std::string &type) -> const char* {
            std::string t = type; t.erase(std::remove_if(t.begin(), t.end(), [](unsigned char c){ return std::isspace(c); }), t.end());
            std::string l = t; std::ranges::transform(l, l.begin(), [](unsigned char c){ return std::tolower(c); });
            if (l.find("wstring") != std::string::npos || l.find("wchar_t*") != std::string::npos) return "L";
            if (l.find("u8string") != std::string::npos || l.find("char8_t*") != std::string::npos) return "u8";
            if (l.find("u16string") != std::string::npos || l.find("char16_t*") != std::string::npos) return "u";
            if (l.find("u32string") != std::string::npos || l.find("char32_t*") != std::string::npos) return "U";
            return "";
        };
        auto is_string_literal = [](std::string s) {
            auto trim = [](std::string &x){ auto l=x.find_first_not_of(" \t\n\r"); auto r=x.find_last_not_of(" \t\n\r"); if(l==std::string::npos){x.clear();} else { x = x.substr(l, r-l+1);} };
            trim(s);
            if (s.size()>=2 && s.front()=='"' && s.back()=='"') return true;
            if (s.size()>=3 && (s[0]=='L' || s[0]=='u' || s[0]=='U') && s[1]=='"' && s.back()=='"') return true;
            if (s.size()>=4 && s[0]=='u' && s[1]=='8' && s[2]=='"' && s.back()=='"') return true;
            return false;
        };
        auto is_char_literal = [](std::string s) {
            auto trim = [](std::string &x){ auto l=x.find_first_not_of(" \t\n\r"); auto r=x.find_last_not_of(" \t\n\r"); if(l==std::string::npos){x.clear();} else { x = x.substr(l, r-l+1);} };
            trim(s);
            if (s.size()>=3 && s.front()=='\'' && s.back()=='\'') return true;
            if (s.size()>=4 && (s[0]=='L' || s[0]=='u' || s[0]=='U') && s[1]=='\'' && s.back()=='\'') return true;
            if (s.size()>=5 && s[0]=='u' && s[1]=='8' && s[2]=='\'' && s.back()=='\'') return true;
            return false;
        };
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
