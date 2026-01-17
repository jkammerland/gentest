// Implementation of AST discovery + validation

#include "discovery.hpp"

#include "axis_expander.hpp"
#include "discovery_utils.hpp"
#include "parse.hpp"
#include "render.hpp"
#include "type_kind.hpp"
#include "validate.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <clang/AST/Decl.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/Basic/SourceManager.h>
#include <fmt/core.h>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <set>
#include <string>
#include <utility>

using namespace clang;
using namespace clang::ast_matchers;
using gentest::codegen::classify_type;
using gentest::codegen::quote_for_type;
using gentest::codegen::TypeKind;

namespace gentest::codegen {

namespace {
[[nodiscard]] std::string derive_namespace_path(const DeclContext *ctx) {
    std::vector<std::string> parts;
    const DeclContext       *current = ctx;
    while (current != nullptr) {
        if (const auto *ns = llvm::dyn_cast<NamespaceDecl>(current)) {
            if (!ns->isAnonymousNamespace()) {
                parts.push_back(ns->getNameAsString());
            }
        }
        current = current->getParent();
    }
    if (parts.empty())
        return std::string{};
    std::reverse(parts.begin(), parts.end());
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out.push_back('/');
        out += parts[i];
    }
    return out;
}
} // namespace

TestCaseCollector::TestCaseCollector(std::vector<TestCaseInfo> &out, bool strict_fixture, bool allow_includes)
    : out_(out), strict_fixture_(strict_fixture), allow_includes_(allow_includes) {}

void TestCaseCollector::run(const MatchFinder::MatchResult &result) {
    const auto *func = result.Nodes.getNodeAs<FunctionDecl>("gentest.func");
    if (func == nullptr) {
        return;
    }

    const auto *sm   = result.SourceManager;
    const auto &lang = result.Context->getLangOpts();
    std::string tu_filename;
    {
        const SourceLocation tu_loc = sm->getLocForStartOfFile(sm->getMainFileID());
        const llvm::StringRef tu_file = sm->getFilename(tu_loc);
        tu_filename = tu_file.str();
    }

    // Allow templated functions; instantiation handled by codegen.

    auto loc = func->getBeginLoc();
    if (loc.isInvalid()) {
        return;
    }
    if (loc.isMacroID()) {
        loc = sm->getExpansionLoc(loc);
    }

    if (!sm->isWrittenInMainFile(loc) && !allow_includes_) {
        return;
    }

    if (sm->isInSystemHeader(loc) || sm->isWrittenInBuiltinFile(loc)) {
        return;
    }

    // Inline classification to support template/parameter expansion
    const auto  collected = collect_gentest_attributes_for(*func, *sm);
    const auto &parsed    = collected.gentest;
    auto        report    = [&](std::string_view message) {
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
    if (parsed.empty())
        return;
    auto summary = validate_attributes(parsed, [&](const std::string &m) {
        had_error_ = true;
        report(m);
    });
    if (!summary.is_case)
        return;
    if (!func->doesThisDeclarationHaveABody())
        return;

    auto report_namespace = [&](const NamespaceDecl &ns, std::string_view message) {
        SourceLocation loc = sm->getSpellingLoc(ns.getBeginLoc());
        if (loc.isInvalid())
            loc = sm->getSpellingLoc(ns.getLocation());
        const llvm::StringRef file = sm->getFilename(loc);
        const unsigned        line = sm->getSpellingLineNumber(loc);
        std::string           name = ns.getQualifiedNameAsString();
        if (name.empty() && ns.isAnonymousNamespace())
            name = "(anonymous namespace)";
        const std::string locpfx = !file.empty() ? fmt::format("{}:{}: ", file.str(), line) : std::string{};
        const std::string subj   = !name.empty() ? fmt::format(" (namespace {})", name) : std::string{};
        llvm::errs() << fmt::format("gentest_codegen: {}{}{}\n", locpfx, message, subj);
    };

    auto find_suite = [&](const DeclContext *ctx) -> std::optional<std::string> {
        const DeclContext *current = ctx;
        while (current != nullptr) {
            if (const auto *ns = llvm::dyn_cast<NamespaceDecl>(current)) {
                auto it = suite_cache_.find(ns);
                if (it == suite_cache_.end()) {
                    auto ns_attrs = collect_gentest_attributes_for(*ns, *sm);
                    for (const auto &msg : ns_attrs.other_namespaces) {
                        report_namespace(*ns, fmt::format("attribute '{}' ignored (unsupported attribute namespace)", msg));
                    }
                    auto summary_ns = validate_namespace_attributes(ns_attrs.gentest, [&](const std::string &m) {
                        had_error_ = true;
                        report_namespace(*ns, m);
                    });
                    it              = suite_cache_.emplace(ns, std::move(summary_ns)).first;
                }
                if (it->second.suite_name.has_value()) {
                    return it->second.suite_name; // explicit override
                }
            }
            current = current->getParent();
        }
        return std::nullopt;
    };

    // 'fixtures(...)' applies only to free functions; reject on member functions.
    if (!summary.fixtures_types.empty()) {
        if (llvm::isa<CXXMethodDecl>(func)) {
            had_error_ = true;
            report("'fixtures(...)' is not supported on member tests");
            return;
        }
    }

    std::string qualified = func->getQualifiedNameAsString();
    if (qualified.empty())
        qualified = func->getNameAsString();
    if (qualified.find("(anonymous namespace)") != std::string::npos) {
        llvm::errs() << fmt::format("gentest_codegen: ignoring test in anonymous namespace: {}\n", qualified);
        return;
    }
    auto file_loc = sm->getFileLoc(func->getLocation());
    auto filename = sm->getFilename(file_loc);
    if (filename.empty())
        return;
    unsigned lnum = sm->getSpellingLineNumber(file_loc);

    // Validate template attribute usage and collect declaration order (optional under flag).
    std::vector<disc::TParam> fn_params_order;
    if (!summary.template_sets.empty()) {
        if (!disc::collect_template_params(*func, fn_params_order)) {
#ifndef GENTEST_DISABLE_TEMPLATE_VALIDATION
            had_error_ = true;
            report("'template(...)' attributes present but function is not a template");
            return;
#else
            fn_params_order.clear(); // fallback to attribute order later
#endif
        }
#ifndef GENTEST_DISABLE_TEMPLATE_VALIDATION
        if (!fn_params_order.empty()) {
            if (!disc::validate_template_attributes(summary.template_sets, fn_params_order,
                                                    [&](const std::string &m) {
                                                        had_error_ = true;
                                                        report(m);
                                                    })) {
                return;
            }
        }
#endif
    }

    // Build combined template argument combinations
    std::vector<std::vector<std::string>> combined_tpl_combos;
    if (!summary.template_sets.empty()) {
#ifndef GENTEST_DISABLE_TEMPLATE_VALIDATION
        if (!fn_params_order.empty()) {
            combined_tpl_combos = disc::build_template_arg_combos(summary.template_sets, fn_params_order);
        } else
#endif
        {
            combined_tpl_combos = disc::build_template_arg_combos_attr_order(summary.template_sets);
        }
    }
    if (combined_tpl_combos.empty())
        combined_tpl_combos.push_back({});

    auto make_qualified = [&](const std::vector<std::string> &tpl_ordered) {
        if (tpl_ordered.empty())
            return qualified;
        std::string q = qualified;
        q += '<';
        for (std::size_t i = 0; i < tpl_ordered.size(); ++i) {
            if (i)
                q += ", ";
            q += tpl_ordered[i];
        }
        q += '>';
        return q;
    };
    auto make_display = [&](const std::string &base, const std::vector<std::string> &tpl_ordered, const std::string &call_args) {
        std::string nm = base;
        if (!tpl_ordered.empty()) {
            nm += '<';
            for (std::size_t i = 0; i < tpl_ordered.size(); ++i) {
                if (i)
                    nm += ",";
                nm += tpl_ordered[i];
            }
            nm += '>';
        }
        if (!call_args.empty()) {
            nm += '(';
            nm += call_args;
            nm += ')';
        }
        return nm;
    };
    // Determine the enclosing scope for qualifying unqualified fixture types
    std::string enclosing_scope;
    if (auto p = qualified.rfind("::"); p != std::string::npos)
        enclosing_scope = qualified.substr(0, p);

    const auto  suite_override = find_suite(func->getDeclContext());
    std::string suite_path     = suite_override.value_or(derive_namespace_path(func->getDeclContext()));
    std::string base_case_name;
    if (summary.case_name.has_value()) {
        base_case_name = *summary.case_name;
    } else if (const auto *method = llvm::dyn_cast<CXXMethodDecl>(func)) {
        const auto *fixture = method->getParent();
        const std::string fixture_name = fixture ? fixture->getNameAsString() : std::string{};
        const std::string method_name  = method->getNameAsString();
        if (!fixture_name.empty()) {
            base_case_name = fixture_name + "/" + method_name;
        } else {
            base_case_name = method_name;
        }
    } else {
        base_case_name = func->getNameAsString();
    }
    std::string final_base     = suite_path.empty() ? base_case_name : (suite_path + "/" + base_case_name);
    // Enforce uniqueness of final base names across this binary
    {
        const SourceLocation  sloc    = sm->getSpellingLoc(func->getBeginLoc());
        const llvm::StringRef file    = sm->getFilename(sloc);
        const unsigned        lnum    = sm->getSpellingLineNumber(sloc);
        const std::string     here    = fmt::format("{}:{}", file.str(), lnum);
        auto                  it_base = unique_base_locations_.find(final_base);
        if (it_base == unique_base_locations_.end()) {
            unique_base_locations_.emplace(final_base, here);
        } else {
            had_error_ = true;
            llvm::errs() << fmt::format("gentest_codegen: duplicate test name '{}' at {} (previously declared at {})\n", final_base, here,
                                        it_base->second);
            return; // do not emit duplicates
        }
    }

    auto add_case = [&](const std::vector<std::string> &tpl_ordered, const std::string &call_args) {
        TestCaseInfo info{};
        info.qualified_name = make_qualified(tpl_ordered);
        info.display_name   = make_display(final_base, tpl_ordered, call_args);
        info.tu_filename    = tu_filename;
        info.filename       = filename.str();
        info.suite_name     = suite_path;
        info.line           = lnum;
        info.tags           = summary.tags;
        info.requirements   = summary.requirements;
        info.should_skip    = summary.should_skip;
        info.skip_reason    = summary.skip_reason;
        info.is_benchmark   = summary.is_benchmark;
        info.is_jitter      = summary.is_jitter;
        info.is_baseline    = summary.is_baseline;
        info.template_args  = tpl_ordered;
        info.call_arguments = call_args;
        info.returns_value  = !func->getReturnType()->isVoidType();
        // Qualify fixture type names if unqualified, using the function's enclosing scope
        if (!summary.fixtures_types.empty()) {
            for (const auto &ty : summary.fixtures_types) {
                if (ty.find("::") != std::string::npos)
                    info.free_fixtures.push_back(ty);
                else if (!enclosing_scope.empty())
                    info.free_fixtures.push_back(enclosing_scope + "::" + ty);
                else
                    info.free_fixtures.push_back(ty);
            }
        }
        if (const auto *method = llvm::dyn_cast<CXXMethodDecl>(func)) {
            if (const auto *record = method->getParent()) {
                const auto class_attrs = collect_gentest_attributes_for(*record, *sm);
                for (const auto &message : class_attrs.other_namespaces)
                    report(fmt::format("attribute '{}' ignored (unsupported attribute namespace)", message));
                auto fixture_summary        = validate_fixture_attributes(class_attrs.gentest, [&](const std::string &m) {
                    had_error_ = true;
                    report(m);
                });
                info.fixture_qualified_name = record->getQualifiedNameAsString();
                if (fixture_summary.lifetime == FixtureLifetime::MemberSuite && suite_path.empty()) {
                    had_error_            = true;
                    report("'fixture(suite)' requires an enclosing named namespace to derive a suite path");
                    info.fixture_lifetime = FixtureLifetime::MemberEphemeral;
                } else {
                    info.fixture_lifetime = fixture_summary.lifetime;
                }

                // Optional strict mode: disallow member tests on suite/global fixtures
                if (info.fixture_lifetime == FixtureLifetime::MemberSuite || info.fixture_lifetime == FixtureLifetime::MemberGlobal) {
                    if (strict_fixture_) {
                        had_error_ = true;
                        if (info.fixture_lifetime == FixtureLifetime::MemberSuite) {
                            report("suite fixtures cannot declare member tests; move assertions to setUp()/tearDown() or use an ephemeral fixture");
                        } else {
                            report("global fixtures cannot declare member tests; move assertions to setUp()/tearDown() or use an ephemeral fixture");
                        }
                    }
                }
            }
        }
        std::string key = info.qualified_name + "#" + info.display_name + "@" + info.filename + ":" + std::to_string(info.line);
        if (seen_.insert(key).second)
            out_.push_back(std::move(info));
    };

    if (!summary.parameter_sets.empty() || !summary.param_packs.empty()) {
        // Build maps of parameter name -> type spelling, and order of parameters
        std::vector<std::string>              param_order;
        std::map<std::string, std::string>    param_types;
        {
            auto policy = PrintingPolicy(result.Context->getLangOpts());
            policy.adjustForCPlusPlus();
            policy.SuppressScope          = false;
            policy.FullyQualifiedName     = true;
            policy.SuppressUnwrittenScope = false;
            for (const ParmVarDecl *p : func->parameters()) {
                std::string name = p->getNameAsString();
                if (name.empty()) {
                    had_error_ = true;
                    report("function parameter is unnamed; named parameters are required for 'parameters(...)'");
                    return;
                }
                std::string ty;
                llvm::raw_string_ostream os(ty);
                p->getType().print(os, policy);
                os.flush();
                param_order.push_back(name);
                param_types.emplace(name, std::move(ty));
            }
        }
        // Validate: all named axes refer to known parameters; no overlaps
        std::set<std::string> seen_param_names;
        for (const auto &ps : summary.parameter_sets) {
            if (!param_types.contains(ps.param_name)) {
                had_error_ = true;
                report(fmt::format("unknown parameter name '{}' in parameters(...)", ps.param_name));
                return;
            }
            if (!seen_param_names.insert(ps.param_name).second) {
                had_error_ = true;
                report(fmt::format("duplicate parameter axis for '{}'", ps.param_name));
                return;
            }
        }
        for (const auto &rs : summary.parameter_ranges) {
            if (!param_types.contains(rs.name)) { had_error_ = true; report(fmt::format("unknown parameter name '{}' in parameters_range(...)", rs.name)); return; }
            if (!seen_param_names.insert(rs.name).second) { had_error_ = true; report(fmt::format("duplicate parameter axis for '{}'", rs.name)); return; }
        }
        for (const auto &ls : summary.parameter_linspaces) {
            if (!param_types.contains(ls.name)) { had_error_ = true; report(fmt::format("unknown parameter name '{}' in parameters_linspace(...)", ls.name)); return; }
            if (!seen_param_names.insert(ls.name).second) { had_error_ = true; report(fmt::format("duplicate parameter axis for '{}'", ls.name)); return; }
        }
        for (const auto &gs : summary.parameter_geoms) {
            if (!param_types.contains(gs.name)) { had_error_ = true; report(fmt::format("unknown parameter name '{}' in parameters_geom(...)", gs.name)); return; }
            if (!seen_param_names.insert(gs.name).second) { had_error_ = true; report(fmt::format("duplicate parameter axis for '{}'", gs.name)); return; }
        }
        for (const auto &pp : summary.param_packs) {
            for (const auto &nm : pp.names) {
                if (!param_types.contains(nm)) {
                    had_error_ = true;
                    report(fmt::format("unknown parameter name '{}' in parameters_pack(...)", nm));
                    return;
                }
                if (!seen_param_names.insert(nm).second) {
                    had_error_ = true;
                    report(fmt::format("parameter '{}' supplied by multiple parameter sets/packs", nm));
                    return;
                }
            }
        }
        // Cartesian product of scalar parameter axes across names
        std::vector<std::vector<std::pair<std::string, std::string>>> scalar_axes; // vector of (name,value) arrays
        for (const auto &ps : summary.parameter_sets) {
            std::vector<std::pair<std::string, std::string>> axis;
            axis.reserve(ps.values.size());
            for (const auto &v : ps.values)
                axis.emplace_back(ps.param_name, v);
            scalar_axes.push_back(std::move(axis));
        }
        // Range-based generators expanded using declared parameter type
        auto normalize = [](std::string s){ s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c){return std::isspace(c)!=0;}), s.end()); std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){return static_cast<char>(std::tolower(c));}); return s; };
        auto is_integer_type = [&](const std::string& ty){ auto t=normalize(ty); if (t.find("bool")!=std::string::npos) return true; if (t.find("char")!=std::string::npos && t.find("char_t")==std::string::npos) return true; if (t.find("short")!=std::string::npos) return true; if (t.find("int")!=std::string::npos) return true; if (t.find("longlong")!=std::string::npos || t=="longlong" || t=="unsignedlonglong") return true; if (t.find("long")!=std::string::npos && t.find("longlong")==std::string::npos) return true; if (t.find("size_t")!=std::string::npos) return true; if (t.find("ptrdiff_t")!=std::string::npos) return true; return false; };
        auto is_float_type = [&](const std::string& ty){ auto t=normalize(ty); return t.find("float")!=std::string::npos || t.find("double")!=std::string::npos; };
        auto to_int = [](const std::string& s){ try { long long v = std::stoll(s); return v; } catch (...) { try { double d=std::stod(s); return static_cast<long long>(std::llround(d)); } catch (...) { return 0LL; } } };
        auto to_double = [](const std::string& s){ try { return std::stod(s); } catch (...) { try { long long v = std::stoll(s); return static_cast<double>(v); } catch (...) { return 0.0; } } };
        auto fmt_int = [](long long v){ return std::to_string(v); };
        auto fmt_double = [](double v){ char buf[64]; std::snprintf(buf, sizeof(buf), "%.17g", v); return std::string(buf); };
        for (const auto &rs : summary.parameter_ranges) {
            const std::string &ty = param_types[rs.name];
            std::vector<std::pair<std::string, std::string>> axis;
            if (is_integer_type(ty)) {
                long long a = to_int(rs.start), st = to_int(rs.step), b = to_int(rs.end);
                if (st == 0) st = 1;
                if ((st > 0 && a > b) || (st < 0 && a < b)) {
                    had_error_ = true; report(fmt::format("parameters_range for '{}' has inconsistent bounds", rs.name)); return;
                }
                if (st > 0) { for (long long v = a; v <= b; v += st) axis.emplace_back(rs.name, fmt_int(v)); }
                else { for (long long v = a; v >= b; v += st) axis.emplace_back(rs.name, fmt_int(v)); }
            } else if (is_float_type(ty)) {
                double a = to_double(rs.start), st = to_double(rs.step), b = to_double(rs.end);
                if (st == 0.0) st = 1.0;
                if ((st > 0.0 && a > b) || (st < 0.0 && a < b)) { had_error_ = true; report(fmt::format("parameters_range for '{}' has inconsistent bounds", rs.name)); return; }
                if (st > 0.0) { for (double v = a; v <= b + st*1e-12; v += st) axis.emplace_back(rs.name, fmt_double(v)); }
                else { for (double v = a; v >= b - (-st)*1e-12; v += st) axis.emplace_back(rs.name, fmt_double(v)); }
            } else {
                had_error_ = true; report(fmt::format("parameters_range not supported for parameter type '{}'", ty)); return;
            }
            scalar_axes.push_back(std::move(axis));
        }
        for (const auto &ls : summary.parameter_linspaces) {
            const std::string &ty = param_types[ls.name];
            std::vector<std::pair<std::string, std::string>> axis;
            long long n = to_int(ls.count); if (n < 1) n = 1;
            if (is_integer_type(ty)) {
                long long a = to_int(ls.start), b = to_int(ls.end);
                if (n == 1) { axis.emplace_back(ls.name, fmt_int(a)); }
                else { double step = static_cast<double>(b - a) / static_cast<double>(n - 1); for (long long i = 0; i < n; ++i) { double d = static_cast<double>(a) + step * static_cast<double>(i); axis.emplace_back(ls.name, fmt_int(static_cast<long long>(std::llround(d)))); } }
            } else if (is_float_type(ty)) {
                double a = to_double(ls.start), b = to_double(ls.end);
                if (n == 1) { axis.emplace_back(ls.name, fmt_double(a)); }
                else { double step = (b - a) / static_cast<double>(n - 1); for (long long i = 0; i < n; ++i) { double d = a + step * static_cast<double>(i); axis.emplace_back(ls.name, fmt_double(d)); } }
            } else { had_error_ = true; report(fmt::format("parameters_linspace not supported for parameter type '{}'", ty)); return; }
            scalar_axes.push_back(std::move(axis));
        }
        for (const auto &gs : summary.parameter_geoms) {
            const std::string &ty = param_types[gs.name];
            std::vector<std::pair<std::string, std::string>> axis;
            long long n = to_int(gs.count); if (n < 1) n = 1;
            if (is_integer_type(ty)) {
                long long a = to_int(gs.start); double f = to_double(gs.factor); double v = static_cast<double>(a); for (long long i=0;i<n;++i){ axis.emplace_back(gs.name, fmt_int(static_cast<long long>(std::llround(v)))); v *= f; }
            } else if (is_float_type(ty)) {
                double a = to_double(gs.start); double f = to_double(gs.factor); double v=a; for (long long i=0;i<n;++i){ axis.emplace_back(gs.name, fmt_double(v)); v *= f; }
            } else { had_error_ = true; report(fmt::format("parameters_geom not supported for parameter type '{}'", ty)); return; }
            scalar_axes.push_back(std::move(axis));
        }
        for (const auto &ls : summary.parameter_logspaces) {
            const std::string &ty = param_types[ls.name];
            std::vector<std::pair<std::string, std::string>> axis;
            long long n = to_int(ls.count); if (n < 1) n = 1;
            double base = ls.base.empty() ? 10.0 : to_double(ls.base);
            double aexp = to_double(ls.start_exp);
            double bexp = to_double(ls.end_exp);
            if (n == 1) {
                double val = std::pow(base, aexp);
                if (is_integer_type(ty)) axis.emplace_back(ls.name, fmt_int(static_cast<long long>(std::llround(val))));
                else if (is_float_type(ty)) axis.emplace_back(ls.name, fmt_double(val));
                else { had_error_ = true; report(fmt::format("logspace not supported for parameter type '{}'", ty)); return; }
            } else {
                double step = (bexp - aexp) / static_cast<double>(n - 1);
                for (long long i = 0; i < n; ++i) {
                    double e = aexp + step * static_cast<double>(i);
                    double val = std::pow(base, e);
                    if (is_integer_type(ty)) axis.emplace_back(ls.name, fmt_int(static_cast<long long>(std::llround(val))));
                    else if (is_float_type(ty)) axis.emplace_back(ls.name, fmt_double(val));
                    else { had_error_ = true; report(fmt::format("logspace not supported for parameter type '{}'", ty)); return; }
                }
            }
            scalar_axes.push_back(std::move(axis));
        }
        std::vector<std::vector<std::pair<std::string, std::string>>> scalar_combos;
        if (scalar_axes.empty()) {
            scalar_combos.push_back({});
        } else {
            // Build cartesian product of name-value pairs
            std::vector<std::size_t> idx(scalar_axes.size(), 0);
            auto total = [&] {
                std::size_t t = 1;
                for (auto &ax : scalar_axes) t *= ax.size();
                return t;
            }();
            scalar_combos.reserve(total);
            std::vector<std::pair<std::string, std::string>> cur;
            std::function<void(std::size_t)> dfs = [&](std::size_t i) {
                if (i == scalar_axes.size()) { scalar_combos.push_back(cur); return; }
                for (const auto &nv : scalar_axes[i]) { cur.push_back(nv); dfs(i + 1); cur.pop_back(); }
            };
            dfs(0);
        }
        // Combine pack rows across multiple packs into maps name->value
        using NameMap = std::map<std::string, std::string>;
        std::vector<NameMap> pack_maps{{}};
        for (const auto &pp : summary.param_packs) {
            std::vector<NameMap> next;
            for (const auto &partial : pack_maps) {
                for (const auto &row : pp.rows) {
                    NameMap nm = partial;
                    for (std::size_t i = 0; i < pp.names.size(); ++i)
                        nm[pp.names[i]] = row[i];
                    next.push_back(std::move(nm));
                }
            }
            pack_maps = std::move(next);
        }
        if (pack_maps.empty()) pack_maps.push_back(NameMap{});

        for (const auto &tpl_combo : combined_tpl_combos) {
            for (const auto &pm : pack_maps) {
                for (const auto &sc : scalar_combos) {
                    // Build combined map name->value
                    NameMap combined = pm;
                    for (const auto &nv : sc) combined[nv.first] = nv.second;
                    // Build call by function parameter order; require all named
                    std::string call;
                    for (std::size_t i = 0; i < param_order.size(); ++i) {
                        if (i) call += ", ";
                        const auto &pname = param_order[i];
                        auto        it    = combined.find(pname);
                        if (it == combined.end()) {
                            had_error_ = true;
                            report(fmt::format("missing value for function parameter '{}' in parameters(...) or parameters_pack(...)", pname));
                            return;
                        }
                        const std::string &ty   = param_types[pname];
                        auto               kind = classify_type(ty);
                        call += quote_for_type(kind, it->second, ty);
                    }
                    add_case(tpl_combo, call);
                }
            }
        }
    } else {
        for (const auto &tpl_combo : combined_tpl_combos)
            add_case(tpl_combo, "");
    }
}

std::optional<TestCaseInfo> TestCaseCollector::classify(const FunctionDecl &func, const SourceManager &sm, const LangOptions &lang) const {
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

    if (!summary.is_case) {
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

    auto report_namespace = [&](const NamespaceDecl &ns, std::string_view message) {
        SourceLocation loc = sm.getSpellingLoc(ns.getBeginLoc());
        if (loc.isInvalid())
            loc = sm.getSpellingLoc(ns.getLocation());
        const llvm::StringRef file = sm.getFilename(loc);
        const unsigned        lnum = sm.getSpellingLineNumber(loc);
        std::string           name = ns.getQualifiedNameAsString();
        if (name.empty() && ns.isAnonymousNamespace())
            name = "(anonymous namespace)";
        const std::string locpfx = !file.empty() ? fmt::format("{}:{}: ", file.str(), lnum) : std::string{};
        const std::string subj   = !name.empty() ? fmt::format(" (namespace {})", name) : std::string{};
        llvm::errs() << fmt::format("gentest_codegen: {}{}{}\n", locpfx, message, subj);
    };

    const auto suite_override = [&]() -> std::optional<std::string> {
        const DeclContext *current = func.getDeclContext();
        while (current != nullptr) {
            if (const auto *ns = llvm::dyn_cast<NamespaceDecl>(current)) {
                auto it = suite_cache_.find(ns);
                if (it == suite_cache_.end()) {
                    auto ns_attrs = collect_gentest_attributes_for(*ns, sm);
                    for (const auto &msg : ns_attrs.other_namespaces) {
                        report_namespace(*ns, fmt::format("attribute '{}' ignored (unsupported attribute namespace)", msg));
                    }
                    auto summary_ns = validate_namespace_attributes(ns_attrs.gentest, [&](const std::string &m) {
                        had_error_ = true;
                        report_namespace(*ns, m);
                    });
                    it              = suite_cache_.emplace(ns, std::move(summary_ns)).first;
                }
                if (it->second.suite_name) {
                    return it->second.suite_name;
                }
            }
            current = current->getParent();
        }
        return std::nullopt;
    }();

    const std::string suite_path = suite_override.value_or(derive_namespace_path(func.getDeclContext()));

    std::string base_case_name;
    if (summary.case_name.has_value()) {
        base_case_name = *summary.case_name;
    } else if (const auto *method = llvm::dyn_cast<CXXMethodDecl>(&func)) {
        const auto *fixture = method->getParent();
        const std::string fixture_name = fixture ? fixture->getNameAsString() : std::string{};
        const std::string method_name  = method->getNameAsString();
        if (!fixture_name.empty()) {
            base_case_name = fixture_name + "/" + method_name;
        } else {
            base_case_name = method_name;
        }
    } else {
        base_case_name = func.getNameAsString();
    }

    std::string display_base = suite_path.empty() ? base_case_name : (suite_path + "/" + base_case_name);

    TestCaseInfo info{};
    info.qualified_name = std::move(qualified);
    info.display_name   = std::move(display_base);
    {
        const SourceLocation tu_loc = sm.getLocForStartOfFile(sm.getMainFileID());
        const llvm::StringRef tu_file = sm.getFilename(tu_loc);
        info.tu_filename = tu_file.str();
    }
    info.filename       = filename.str();
    info.suite_name     = suite_path;
    info.line           = line;
    info.tags           = std::move(summary.tags);
    info.requirements   = std::move(summary.requirements);
    info.should_skip    = summary.should_skip;
    info.skip_reason    = std::move(summary.skip_reason);
    info.is_benchmark   = summary.is_benchmark;
    info.is_jitter      = summary.is_jitter;
    info.is_baseline    = summary.is_baseline;
    info.returns_value  = !func.getReturnType()->isVoidType();

    // If this is a method, collect fixture attributes from the parent class/struct.
    if (const auto *method = llvm::dyn_cast<CXXMethodDecl>(&func)) {
        if (const auto *record = method->getParent()) {
            const auto class_attrs = collect_gentest_attributes_for(*record, sm);
            for (const auto &message : class_attrs.other_namespaces) {
                report(fmt::format("attribute '{}' ignored (unsupported attribute namespace)", message));
            }
            auto fixture_summary        = validate_fixture_attributes(class_attrs.gentest, [&](const std::string &m) {
                had_error_ = true;
                report(m);
            });
            info.fixture_qualified_name = record->getQualifiedNameAsString();
            if (fixture_summary.lifetime == FixtureLifetime::MemberSuite && suite_path.empty()) {
                had_error_            = true;
                report("'fixture(suite)' requires an enclosing named namespace to derive a suite path");
                info.fixture_lifetime = FixtureLifetime::MemberEphemeral;
            } else {
                info.fixture_lifetime = fixture_summary.lifetime;
            }

            // Optional strict mode: disallow member tests on suite/global fixtures
            if (info.fixture_lifetime == FixtureLifetime::MemberSuite || info.fixture_lifetime == FixtureLifetime::MemberGlobal) {
                if (strict_fixture_) {
                    had_error_ = true;
                    if (info.fixture_lifetime == FixtureLifetime::MemberSuite) {
                        report("suite fixtures cannot declare member tests; move assertions to setUp()/tearDown() or use an ephemeral fixture");
                    } else {
                        report("global fixtures cannot declare member tests; move assertions to setUp()/tearDown() or use an ephemeral fixture");
                    }
                }
            }
        }
    }
    return info;
}

bool TestCaseCollector::has_errors() const { return had_error_; }

} // namespace gentest::codegen
