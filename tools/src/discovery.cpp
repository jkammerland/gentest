// Implementation of AST discovery + validation

#include "discovery.hpp"

#include "axis_expander.hpp"
#include "discovery_utils.hpp"
#include "log.hpp"
#include "parse.hpp"
#include "render.hpp"
#include "type_kind.hpp"
#include "validate.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <clang/AST/Decl.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/Basic/SourceManager.h>
#include <fmt/format.h>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

using namespace clang;
using namespace clang::ast_matchers;
using gentest::codegen::classify_type;
using gentest::codegen::quote_for_type;
using gentest::codegen::TypeKind;

namespace gentest::codegen {

namespace {
bool ends_with_ci(llvm::StringRef text, llvm::StringRef suffix) {
    if (text.size() < suffix.size()) {
        return false;
    }
    llvm::StringRef tail = text.take_back(suffix.size());
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const auto to_lower = [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); };
        if (to_lower(static_cast<unsigned char>(tail[i])) != to_lower(static_cast<unsigned char>(suffix[i]))) {
            return false;
        }
    }
    return true;
}

bool has_cpp_extension(llvm::StringRef path) {
    return ends_with_ci(path, ".cc") || ends_with_ci(path, ".cpp") || ends_with_ci(path, ".cxx");
}

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
    std::ranges::reverse(parts);
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out.push_back('/');
        out += parts[i];
    }
    return out;
}

[[nodiscard]] std::vector<std::string> collect_namespace_parts(const DeclContext *ctx) {
    std::vector<std::string> parts;
    const DeclContext *current = ctx;
    while (current != nullptr) {
        if (const auto *ns = llvm::dyn_cast<NamespaceDecl>(current)) {
            if (!ns->isAnonymousNamespace()) {
                parts.push_back(ns->getNameAsString());
            }
        }
        current = current->getParent();
    }
    if (parts.empty()) {
        return parts;
    }
    std::ranges::reverse(parts);
    return parts;
}

std::string print_type(QualType type, const PrintingPolicy &policy) {
    std::string             out;
    llvm::raw_string_ostream os(out);
    type.print(os, policy);
    os.flush();
    return out;
}

bool is_std_namespace(const DeclContext *ctx) {
    const auto *current = ctx;
    while (current != nullptr) {
        if (const auto *ns = llvm::dyn_cast<NamespaceDecl>(current)) {
            if (ns->isInlineNamespace()) {
                current = ns->getParent();
                continue;
            }
            return ns->getName() == "std";
        }
        current = current->getParent();
    }
    return false;
}

std::optional<QualType> unwrap_std_shared_ptr(QualType type) {
    type = type.getCanonicalType().getUnqualifiedType();
    const auto *record_type = type->getAs<RecordType>();
    if (!record_type) {
        return std::nullopt;
    }
    const auto *spec = llvm::dyn_cast<ClassTemplateSpecializationDecl>(record_type->getDecl());
    if (!spec) {
        return std::nullopt;
    }
    if (spec->getName() != "shared_ptr") {
        return std::nullopt;
    }
    if (!is_std_namespace(spec->getDeclContext())) {
        return std::nullopt;
    }
    const auto &args = spec->getTemplateArgs();
    if (args.size() != 1 || args[0].getKind() != TemplateArgument::Type) {
        return std::nullopt;
    }
    return args[0].getAsType();
}

std::string infer_fixture_type_from_param(const ParmVarDecl &param, const PrintingPolicy &policy) {
    QualType type = param.getType();
    if (type->isReferenceType()) {
        type = type->getPointeeType();
    }
    type = type.getUnqualifiedType();

    if (const auto *pointer = type->getAs<PointerType>()) {
        type = pointer->getPointeeType().getUnqualifiedType();
    }
    if (const auto shared_inner = unwrap_std_shared_ptr(type.getUnqualifiedType())) {
        type = shared_inner->getUnqualifiedType();
    }
    type = type.getUnqualifiedType();
    const QualType canonical = type.getCanonicalType().getUnqualifiedType();
    return print_type(canonical, policy);
}

std::optional<FixtureScope> infer_declared_fixture_scope_from_param(const ParmVarDecl &param, const SourceManager &sm) {
    QualType type = param.getType();
    if (type->isReferenceType()) {
        type = type->getPointeeType();
    }
    type = type.getUnqualifiedType();
    if (const auto *pointer = type->getAs<PointerType>()) {
        type = pointer->getPointeeType().getUnqualifiedType();
    }
    if (const auto shared_inner = unwrap_std_shared_ptr(type.getUnqualifiedType())) {
        type = shared_inner->getUnqualifiedType();
    }
    type = type.getUnqualifiedType();

    const auto *record = type->getAsCXXRecordDecl();
    if (!record) {
        return std::nullopt;
    }
    if (const auto *definition = record->getDefinition()) {
        record = definition;
    }

    const auto attrs = collect_gentest_attributes_for(*record, sm);
    const auto summary = validate_fixture_attributes(attrs.gentest, [](const std::string &) {});
    if (summary.lifetime == FixtureLifetime::MemberSuite) {
        return FixtureScope::Suite;
    }
    if (summary.lifetime == FixtureLifetime::MemberGlobal) {
        return FixtureScope::Global;
    }
    return std::nullopt;
}

struct FunctionParamInfo {
    const ParmVarDecl* decl = nullptr;
    std::string name;
    std::string type_spelling;
    bool        is_value_axis = false;
    bool        has_default_arg = false;
    bool        is_fixture = false;
    std::size_t fixture_index = 0;
    std::optional<FixtureScope> required_scope;
};
} // namespace

TestCaseCollector::TestCaseCollector(std::vector<TestCaseInfo> &out, bool strict_fixture, bool allow_includes)
    : out_(out), strict_fixture_(strict_fixture), allow_includes_(allow_includes) {}

void TestCaseCollector::run(const MatchFinder::MatchResult &result) {
    const auto *func = result.Nodes.getNodeAs<FunctionDecl>("gentest.func");
    if (func == nullptr) {
        return;
    }

    const auto *sm   = result.SourceManager;

    // Allow templated functions; instantiation handled by codegen.

    auto loc = func->getBeginLoc();
    if (loc.isInvalid()) {
        return;
    }
    if (loc.isMacroID()) {
        loc = sm->getExpansionLoc(loc);
    }

    const bool in_main_file = sm->isWrittenInMainFile(loc);
    if (!in_main_file) {
        if (!allow_includes_) {
            return;
        }
        // TU shim mode: the shim includes a single original translation unit
        // (*.cc/*.cpp/*.cxx). Avoid discovering tests in headers, since the
        // build system does not yet track header deps for codegen.
        const llvm::StringRef inc_file = sm->getFilename(loc);
        if (!has_cpp_extension(inc_file)) {
            return;
        }
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
        std::string           locpfx;
        if (!file.empty()) {
            locpfx.reserve(file.size() + 24);
            fmt::format_to(std::back_inserter(locpfx), "{}:{}: ", file.str(), lnum);
        }
        std::string subj;
        if (!subject.empty()) {
            subj.reserve(subject.size() + 4);
            fmt::format_to(std::back_inserter(subj), " ({})", subject);
        }
        log_err("gentest_codegen: {}{}{}\n", locpfx, message, subj);
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
        std::string locpfx;
        if (!file.empty()) {
            locpfx.reserve(file.size() + 24);
            fmt::format_to(std::back_inserter(locpfx), "{}:{}: ", file.str(), line);
        }
        std::string subj;
        if (!name.empty()) {
            subj.reserve(name.size() + 16);
            fmt::format_to(std::back_inserter(subj), " (namespace {})", name);
        }
        log_err("gentest_codegen: {}{}{}\n", locpfx, message, subj);
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

    std::string qualified = func->getQualifiedNameAsString();
    if (qualified.empty())
        qualified = func->getNameAsString();
    if (qualified.find("(anonymous namespace)") != std::string::npos) {
        log_err("gentest_codegen: ignoring test in anonymous namespace: {}\n", qualified);
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
        combined_tpl_combos.emplace_back();

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
    const std::vector<std::string> namespace_parts = collect_namespace_parts(func->getDeclContext());

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
            log_err("gentest_codegen: duplicate test name '{}' at {} (previously declared at {})\n", final_base, here, it_base->second);
            return; // do not emit duplicates
        }
    }

    std::string tu_filename;
    {
        const SourceLocation  tu_loc  = sm->getLocForStartOfFile(sm->getMainFileID());
        const llvm::StringRef tu_file = sm->getFilename(tu_loc);
        tu_filename = tu_file.str();
    }

    struct FixtureContext {
        std::string    qualified_name;
        FixtureLifetime lifetime = FixtureLifetime::MemberEphemeral;
    };
    std::optional<FixtureContext> fixture_ctx;
    if (const auto *method = llvm::dyn_cast<CXXMethodDecl>(func)) {
        if (const auto *record = method->getParent()) {
            const auto class_attrs = collect_gentest_attributes_for(*record, *sm);
            for (const auto &message : class_attrs.other_namespaces)
                report(fmt::format("attribute '{}' ignored (unsupported attribute namespace)", message));
            auto fixture_summary = validate_fixture_attributes(class_attrs.gentest, [&](const std::string &m) {
                had_error_ = true;
                report(m);
            });
            FixtureContext ctx;
            ctx.qualified_name = record->getQualifiedNameAsString();
            if (fixture_summary.lifetime == FixtureLifetime::MemberSuite && suite_path.empty()) {
                had_error_  = true;
                report("'fixture(suite)' requires an enclosing named namespace to derive a suite path");
                ctx.lifetime = FixtureLifetime::MemberEphemeral;
            } else {
                ctx.lifetime = fixture_summary.lifetime;
            }
            if (ctx.lifetime == FixtureLifetime::MemberSuite || ctx.lifetime == FixtureLifetime::MemberGlobal) {
                if (strict_fixture_) {
                    had_error_ = true;
                    if (ctx.lifetime == FixtureLifetime::MemberSuite) {
                        report("suite fixtures cannot declare member tests; move assertions to setUp()/tearDown() or use an ephemeral fixture");
                    } else {
                        report("global fixtures cannot declare member tests; move assertions to setUp()/tearDown() or use an ephemeral fixture");
                    }
                }
            }
            fixture_ctx = std::move(ctx);
        }
    }

    auto add_case = [&](const std::vector<std::string> &tpl_ordered, const std::string &display_args, const std::string &call_args,
                        const std::vector<std::string> &free_fixture_types,
                        const std::vector<std::optional<FixtureScope>> &free_fixture_required_scopes,
                        const std::vector<FreeCallArg> &free_call_args) {
        TestCaseInfo info{};
        info.qualified_name = make_qualified(tpl_ordered);
        info.display_name   = make_display(final_base, tpl_ordered, display_args);
        info.base_name      = final_base;
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
        info.returns_value = !func->getReturnType()->isVoidType();
        info.namespace_parts = namespace_parts;
        info.free_fixture_types           = free_fixture_types;
        info.free_fixture_required_scopes = free_fixture_required_scopes;
        info.free_call_args               = free_call_args;
        if (fixture_ctx) {
            info.fixture_qualified_name = fixture_ctx->qualified_name;
            info.fixture_lifetime       = fixture_ctx->lifetime;
        }
        std::string key = info.qualified_name + "#" + info.display_name + "@" + info.filename + ":" + std::to_string(info.line);
        if (seen_.insert(key).second)
            out_.push_back(std::move(info));
    };

    auto policy = PrintingPolicy(result.Context->getLangOpts());
    policy.adjustForCPlusPlus();
    policy.SuppressScope          = false;
    policy.FullyQualifiedName     = true;
    policy.SuppressUnwrittenScope = false;

    std::vector<FunctionParamInfo> function_params;
    function_params.reserve(func->parameters().size());
    std::map<std::string, std::string> param_types;
    for (const ParmVarDecl *p : func->parameters()) {
        FunctionParamInfo param{};
        param.decl = p;
        param.name = p->getNameAsString();
        param.type_spelling = print_type(p->getType(), policy);
        param.has_default_arg = p->hasDefaultArg();
        if (!param.name.empty()) {
            param_types.emplace(param.name, param.type_spelling);
        }
        function_params.push_back(std::move(param));
    }

    // Validate: all named axes refer to known parameters; no overlaps.
    std::set<std::string> value_axis_names;
    auto validate_axis_name = [&](const std::string &name, std::string_view origin) -> bool {
        if (!param_types.contains(name)) {
            had_error_ = true;
            report(fmt::format("unknown parameter name '{}' in {}(...)", name, origin));
            return false;
        }
        if (!value_axis_names.insert(name).second) {
            had_error_ = true;
            report(fmt::format("duplicate parameter axis for '{}'", name));
            return false;
        }
        return true;
    };

    for (const auto &ps : summary.parameter_sets) {
        if (!validate_axis_name(ps.param_name, "parameters")) {
            return;
        }
    }
    for (const auto &rs : summary.parameter_ranges) {
        if (!validate_axis_name(rs.name, "parameters_range")) {
            return;
        }
    }
    for (const auto &ls : summary.parameter_linspaces) {
        if (!validate_axis_name(ls.name, "parameters_linspace")) {
            return;
        }
    }
    for (const auto &gs : summary.parameter_geoms) {
        if (!validate_axis_name(gs.name, "parameters_geom")) {
            return;
        }
    }
    for (const auto &ls : summary.parameter_logspaces) {
        if (!validate_axis_name(ls.name, "logspace")) {
            return;
        }
    }
    for (const auto &pp : summary.param_packs) {
        for (const auto &nm : pp.names) {
            if (!param_types.contains(nm)) {
                had_error_ = true;
                report(fmt::format("unknown parameter name '{}' in parameters_pack(...)", nm));
                return;
            }
            if (!value_axis_names.insert(nm).second) {
                had_error_ = true;
                report(fmt::format("parameter '{}' supplied by multiple parameter sets/packs", nm));
                return;
            }
        }
    }

    std::vector<std::string> inferred_fixture_types;
    std::vector<std::optional<FixtureScope>> inferred_fixture_required_scopes;
    for (auto &param : function_params) {
        if (!param.name.empty() && value_axis_names.contains(param.name)) {
            param.is_value_axis = true;
            continue;
        }
        if (param.has_default_arg) {
            continue;
        }
        param.is_fixture = true;
        param.fixture_index = inferred_fixture_types.size();
        if (!param.decl) {
            had_error_ = true;
            report("internal error while inferring fixture parameter type");
            return;
        }
        std::string fixture_type = infer_fixture_type_from_param(*param.decl, policy);
        if (fixture_type.empty()) {
            had_error_ = true;
            const std::string fallback_name = param.name.empty() ? std::string("<unnamed>") : param.name;
            report(fmt::format("unable to infer fixture type for parameter '{}'", fallback_name));
            return;
        }
        param.required_scope = infer_declared_fixture_scope_from_param(*param.decl, *sm);
        inferred_fixture_types.push_back(std::move(fixture_type));
        inferred_fixture_required_scopes.push_back(param.required_scope);
    }

    // Cartesian product of scalar parameter axes across names.
    std::vector<std::vector<std::pair<std::string, std::string>>> scalar_axes; // vector of (name,value) arrays
    for (const auto &ps : summary.parameter_sets) {
        std::vector<std::pair<std::string, std::string>> axis;
        axis.reserve(ps.values.size());
        for (const auto &v : ps.values) {
            axis.emplace_back(ps.param_name, v);
        }
        scalar_axes.push_back(std::move(axis));
    }

    // Range-based generators expanded using declared parameter type.
    auto normalize_type = [](std::string_view text) -> std::string {
        std::string out(text);
        std::erase_if(out, [](unsigned char c) { return std::isspace(c) != 0; });
        std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    };
    std::unordered_map<std::string, std::string> param_types_norm;
    param_types_norm.reserve(param_types.size());
    for (const auto &[name, ty] : param_types) {
        param_types_norm.emplace(name, normalize_type(ty));
    }
    auto is_integer_type = [&](const std::string &t) -> bool {
        if (t.find("bool") != std::string::npos)
            return true;
        if (t.find("char") != std::string::npos && t.find("char_t") == std::string::npos)
            return true;
        if (t.find("short") != std::string::npos)
            return true;
        if (t.find("int") != std::string::npos)
            return true;
        if (t.find("longlong") != std::string::npos || t == "longlong" || t == "unsignedlonglong")
            return true;
        if (t.find("long") != std::string::npos && t.find("longlong") == std::string::npos)
            return true;
        if (t.find("size_t") != std::string::npos)
            return true;
        if (t.find("ptrdiff_t") != std::string::npos)
            return true;
        return false;
    };
    auto is_float_type = [&](const std::string &t) -> bool {
        return t.find("float") != std::string::npos || t.find("double") != std::string::npos;
    };
    auto to_int = [](std::string_view s) -> long long {
        try {
            return std::stoll(std::string(s));
        } catch (...) {
            try {
                const double d = std::stod(std::string(s));
                return static_cast<long long>(std::llround(d));
            } catch (...) {
                return 0LL;
            }
        }
    };
    auto to_double = [](std::string_view s) -> double {
        try {
            return std::stod(std::string(s));
        } catch (...) {
            try {
                return static_cast<double>(std::stoll(std::string(s)));
            } catch (...) {
                return 0.0;
            }
        }
    };
    auto fmt_int = [](long long v) -> std::string { return std::to_string(v); };
    auto fmt_double = [](double v) -> std::string {
        char buf[64];
        (void)std::snprintf(buf, sizeof(buf), "%.17g", v);
        return {buf};
    };
    for (const auto &rs : summary.parameter_ranges) {
        const std::string &ty = param_types_norm.at(rs.name);
        std::vector<std::pair<std::string, std::string>> axis;
        if (is_integer_type(ty)) {
            long long a = to_int(rs.start), st = to_int(rs.step), b = to_int(rs.end);
            if (st == 0) st = 1;
            if ((st > 0 && a > b) || (st < 0 && a < b)) {
                had_error_ = true;
                report(fmt::format("parameters_range for '{}' has inconsistent bounds", rs.name));
                return;
            }
            if (st > 0) {
                for (long long v = a; v <= b; v += st)
                    axis.emplace_back(rs.name, fmt_int(v));
            } else {
                for (long long v = a; v >= b; v += st)
                    axis.emplace_back(rs.name, fmt_int(v));
            }
        } else if (is_float_type(ty)) {
            const double a  = to_double(rs.start);
            double       st = to_double(rs.step);
            const double b  = to_double(rs.end);
            if (st == 0.0)
                st = 1.0;
            if ((st > 0.0 && a > b) || (st < 0.0 && a < b)) {
                had_error_ = true;
                report(fmt::format("parameters_range for '{}' has inconsistent bounds", rs.name));
                return;
            }

            const double eps = std::abs(st) * 1e-12;
            for (std::size_t i = 0;; ++i) {
                const double v = a + st * static_cast<double>(i);
                if (!std::isfinite(v)) {
                    had_error_ = true;
                    report(fmt::format("parameters_range for '{}' produced a non-finite value", rs.name));
                    return;
                }
                if (st > 0.0) {
                    if (v > b + eps)
                        break;
                } else {
                    if (v < b - eps)
                        break;
                }
                axis.emplace_back(rs.name, fmt_double(v));
            }
        } else {
            had_error_ = true;
            report(fmt::format("parameters_range not supported for parameter type '{}'", ty));
            return;
        }
        scalar_axes.push_back(std::move(axis));
    }
    for (const auto &ls : summary.parameter_linspaces) {
        const std::string &ty = param_types_norm.at(ls.name);
        std::vector<std::pair<std::string, std::string>> axis;
        long long n = to_int(ls.count);
        if (n < 1)
            n = 1;
        if (is_integer_type(ty)) {
            long long a = to_int(ls.start), b = to_int(ls.end);
            if (n == 1) {
                axis.emplace_back(ls.name, fmt_int(a));
            } else {
                const double step = static_cast<double>(b - a) / static_cast<double>(n - 1);
                for (long long i = 0; i < n; ++i) {
                    const double d = static_cast<double>(a) + step * static_cast<double>(i);
                    axis.emplace_back(ls.name, fmt_int(static_cast<long long>(std::llround(d))));
                }
            }
        } else if (is_float_type(ty)) {
            double a = to_double(ls.start), b = to_double(ls.end);
            if (n == 1) {
                axis.emplace_back(ls.name, fmt_double(a));
            } else {
                const double step = (b - a) / static_cast<double>(n - 1);
                for (long long i = 0; i < n; ++i) {
                    const double d = a + step * static_cast<double>(i);
                    axis.emplace_back(ls.name, fmt_double(d));
                }
            }
        } else {
            had_error_ = true;
            report(fmt::format("parameters_linspace not supported for parameter type '{}'", ty));
            return;
        }
        scalar_axes.push_back(std::move(axis));
    }
    for (const auto &gs : summary.parameter_geoms) {
        const std::string &ty = param_types_norm.at(gs.name);
        std::vector<std::pair<std::string, std::string>> axis;
        long long n = to_int(gs.count);
        if (n < 1)
            n = 1;
        if (is_integer_type(ty)) {
            const long long a = to_int(gs.start);
            const double    f = to_double(gs.factor);
            auto            v = static_cast<double>(a);
            for (long long i = 0; i < n; ++i) {
                axis.emplace_back(gs.name, fmt_int(static_cast<long long>(std::llround(v))));
                v *= f;
            }
        } else if (is_float_type(ty)) {
            const double a = to_double(gs.start);
            const double f = to_double(gs.factor);
            auto         v = a;
            for (long long i = 0; i < n; ++i) {
                axis.emplace_back(gs.name, fmt_double(v));
                v *= f;
            }
        } else {
            had_error_ = true;
            report(fmt::format("parameters_geom not supported for parameter type '{}'", ty));
            return;
        }
        scalar_axes.push_back(std::move(axis));
    }
    for (const auto &ls : summary.parameter_logspaces) {
        const std::string &ty = param_types_norm.at(ls.name);
        std::vector<std::pair<std::string, std::string>> axis;
        long long n = to_int(ls.count);
        if (n < 1)
            n = 1;
        const double base = ls.base.empty() ? 10.0 : to_double(ls.base);
        const double aexp = to_double(ls.start_exp);
        const double bexp = to_double(ls.end_exp);
        if (n == 1) {
            const double val = std::pow(base, aexp);
            if (is_integer_type(ty))
                axis.emplace_back(ls.name, fmt_int(static_cast<long long>(std::llround(val))));
            else if (is_float_type(ty))
                axis.emplace_back(ls.name, fmt_double(val));
            else {
                had_error_ = true;
                report(fmt::format("logspace not supported for parameter type '{}'", ty));
                return;
            }
        } else {
            const double step = (bexp - aexp) / static_cast<double>(n - 1);
            for (long long i = 0; i < n; ++i) {
                const double e   = aexp + step * static_cast<double>(i);
                const double val = std::pow(base, e);
                if (is_integer_type(ty))
                    axis.emplace_back(ls.name, fmt_int(static_cast<long long>(std::llround(val))));
                else if (is_float_type(ty))
                    axis.emplace_back(ls.name, fmt_double(val));
                else {
                    had_error_ = true;
                    report(fmt::format("logspace not supported for parameter type '{}'", ty));
                    return;
                }
            }
        }
        scalar_axes.push_back(std::move(axis));
    }

    auto join_csv = [](const std::vector<std::string> &parts) {
        std::string out;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i) out += ", ";
            out += parts[i];
        }
        return out;
    };

    using NameMap = std::map<std::string, std::string>;
    NameMap current;

    auto set_value = [&](const std::string &name, const std::string &value) -> std::optional<std::string> {
        auto it = current.find(name);
        if (it == current.end()) {
            current.emplace(name, value);
            return std::nullopt;
        }
        std::string prev = it->second;
        it->second       = value;
        return prev;
    };
    auto restore_value = [&](const std::string &name, const std::optional<std::string> &prev) {
        if (prev.has_value())
            current[name] = *prev;
        else
            current.erase(name);
    };

    auto emit_case = [&](const std::vector<std::string> &tpl_combo) {
        std::vector<std::string> value_exprs_for_display;
        std::vector<std::string> value_exprs_for_call;
        std::vector<FreeCallArg> free_call_args;
        free_call_args.reserve(function_params.size());

        for (const auto &param : function_params) {
            if (param.is_fixture) {
                FreeCallArg arg{};
                arg.kind          = FreeCallArgKind::Fixture;
                arg.fixture_index = param.fixture_index;
                free_call_args.push_back(std::move(arg));
                continue;
            }
            if (!param.is_value_axis) {
                // Parameter has a default argument and is not driven by explicit value axes.
                continue;
            }

            if (param.name.empty()) {
                had_error_ = true;
                report("missing value for unnamed function parameter in parameters(...) or parameters_pack(...)");
                return;
            }
            auto it = current.find(param.name);
            if (it == current.end()) {
                had_error_ = true;
                report(fmt::format("missing value for function parameter '{}' in parameters(...) or parameters_pack(...)", param.name));
                return;
            }
            auto kind = classify_type(param.type_spelling);
            std::string expr = quote_for_type(kind, it->second, param.type_spelling);
            value_exprs_for_call.push_back(expr);
            value_exprs_for_display.push_back(expr);
            FreeCallArg arg{};
            arg.kind = FreeCallArgKind::Value;
            arg.value_expression = std::move(expr);
            free_call_args.push_back(std::move(arg));
        }

        const std::string display_args = join_csv(value_exprs_for_display);
        const std::string call_args    = join_csv(value_exprs_for_call);
        add_case(tpl_combo, display_args, call_args, inferred_fixture_types, inferred_fixture_required_scopes, free_call_args);
    };

    std::function<void(std::size_t, const std::vector<std::string> &)> visit_scalars =
        [&](std::size_t idx, const std::vector<std::string> &tpl_combo) {
            if (idx == scalar_axes.size()) {
                emit_case(tpl_combo);
                return;
            }
            for (const auto &nv : scalar_axes[idx]) {
                const auto prev = set_value(nv.first, nv.second);
                visit_scalars(idx + 1, tpl_combo);
                restore_value(nv.first, prev);
            }
        };

    std::function<void(std::size_t, const std::vector<std::string> &)> visit_packs =
        [&](std::size_t idx, const std::vector<std::string> &tpl_combo) {
            if (idx == summary.param_packs.size()) {
                if (scalar_axes.empty())
                    emit_case(tpl_combo);
                else
                    visit_scalars(0, tpl_combo);
                return;
            }
            const auto &pp = summary.param_packs[idx];
            for (const auto &row : pp.rows) {
                std::vector<std::pair<std::string, std::optional<std::string>>> prev;
                prev.reserve(pp.names.size());
                for (std::size_t i = 0; i < pp.names.size(); ++i) {
                    prev.emplace_back(pp.names[i], set_value(pp.names[i], row[i]));
                }
                visit_packs(idx + 1, tpl_combo);
                for (auto it = prev.rbegin(); it != prev.rend(); ++it) {
                    restore_value(it->first, it->second);
                }
            }
        };

    for (const auto &tpl_combo : combined_tpl_combos) {
        if (summary.param_packs.empty()) {
            if (scalar_axes.empty())
                emit_case(tpl_combo);
            else
                visit_scalars(0, tpl_combo);
        } else {
            visit_packs(0, tpl_combo);
        }
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
        log_err("gentest_codegen: {}{}{}\n", locpfx, message, subj);
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
        log_err("gentest_codegen: ignoring test in anonymous namespace: {}\n", qualified);
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
        log_err("gentest_codegen: {}{}{}\n", locpfx, message, subj);
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
    info.namespace_parts = collect_namespace_parts(func.getDeclContext());

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

FixtureDeclCollector::FixtureDeclCollector(std::vector<FixtureDeclInfo> &out)
    : out_(out) {}

void FixtureDeclCollector::report(const clang::CXXRecordDecl &decl, const clang::SourceManager &sm, std::string_view message) const {
    const SourceLocation  loc     = sm.getSpellingLoc(decl.getBeginLoc());
    const llvm::StringRef file    = sm.getFilename(loc);
    const unsigned        line    = sm.getSpellingLineNumber(loc);
    const std::string     subject = decl.getQualifiedNameAsString();
    const std::string     locpfx  = !file.empty() ? fmt::format("{}:{}: ", file.str(), line) : std::string{};
    const std::string     subj    = !subject.empty() ? fmt::format(" ({})", subject) : std::string{};
    log_err("gentest_codegen: {}{}{}\n", locpfx, message, subj);
}

void FixtureDeclCollector::run(const MatchFinder::MatchResult &result) {
    const auto *record = result.Nodes.getNodeAs<CXXRecordDecl>("gentest.fixture");
    if (record == nullptr) {
        return;
    }
    if (!record->isThisDeclarationADefinition()) {
        return;
    }
    const auto *sm = result.SourceManager;
    auto loc = record->getBeginLoc();
    if (loc.isInvalid()) {
        return;
    }
    if (loc.isMacroID()) {
        loc = sm->getExpansionLoc(loc);
    }
    if (sm->isInSystemHeader(loc) || sm->isWrittenInBuiltinFile(loc)) {
        return;
    }

    const auto attrs = collect_gentest_attributes_for(*record, *sm);
    for (const auto &message : attrs.other_namespaces) {
        report(*record, *sm, fmt::format("attribute '{}' ignored (unsupported attribute namespace)", message));
    }
    auto summary = validate_fixture_attributes(attrs.gentest, [&](const std::string &m) {
        had_error_ = true;
        report(*record, *sm, m);
    });

    if (summary.had_error) {
        had_error_ = true;
        return;
    }

    if (summary.lifetime != FixtureLifetime::MemberSuite && summary.lifetime != FixtureLifetime::MemberGlobal) {
        return; // not a shared fixture declaration
    }

    auto report_namespace = [&](const NamespaceDecl &ns, std::string_view message) {
        SourceLocation loc = sm->getSpellingLoc(ns.getBeginLoc());
        if (loc.isInvalid())
            loc = sm->getSpellingLoc(ns.getLocation());
        const llvm::StringRef file = sm->getFilename(loc);
        const unsigned        lnum = sm->getSpellingLineNumber(loc);
        std::string           name = ns.getQualifiedNameAsString();
        if (name.empty() && ns.isAnonymousNamespace())
            name = "(anonymous namespace)";
        const std::string locpfx = !file.empty() ? fmt::format("{}:{}: ", file.str(), lnum) : std::string{};
        const std::string subj   = !name.empty() ? fmt::format(" (namespace {})", name) : std::string{};
        log_err("gentest_codegen: {}{}{}\n", locpfx, message, subj);
    };

    const auto suite_override = [&]() -> std::optional<std::string> {
        const DeclContext *current = record->getDeclContext();
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
                    it = suite_cache_.emplace(ns, std::move(summary_ns)).first;
                }
                if (it->second.suite_name) {
                    return it->second.suite_name;
                }
            }
            current = current->getParent();
        }
        return std::nullopt;
    }();

    const std::string suite_path = suite_override.value_or(derive_namespace_path(record->getDeclContext()));
    if (summary.lifetime == FixtureLifetime::MemberSuite && suite_path.empty()) {
        had_error_ = true;
        report(*record, *sm, "'fixture(suite)' requires an enclosing named namespace to derive a suite path");
        return;
    }

    FixtureDeclInfo info{};
    info.qualified_name  = record->getQualifiedNameAsString();
    info.base_name       = record->getNameAsString();
    info.namespace_parts = collect_namespace_parts(record->getDeclContext());
    info.suite_name      = suite_path;
    info.scope           = (summary.lifetime == FixtureLifetime::MemberSuite) ? FixtureScope::Suite : FixtureScope::Global;
    {
        const SourceLocation tu_loc = sm->getLocForStartOfFile(sm->getMainFileID());
        const llvm::StringRef tu_file = sm->getFilename(tu_loc);
        info.tu_filename = tu_file.str();
    }
    {
        const SourceLocation file_loc = sm->getFileLoc(record->getLocation());
        const llvm::StringRef file = sm->getFilename(file_loc);
        info.filename = file.str();
        info.line = sm->getSpellingLineNumber(file_loc);
    }
    out_.push_back(std::move(info));
}

bool FixtureDeclCollector::has_errors() const { return had_error_; }

namespace {
struct ParsedFixtureType {
    std::string base;
    std::string suffix;
    std::string full;
    bool        qualified = false;
};

std::string trim_copy(std::string_view text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(start, end - start));
}

ParsedFixtureType parse_fixture_type(std::string_view text) {
    ParsedFixtureType out{};
    out.full = trim_copy(text);
    int depth = 0;
    std::size_t split = std::string::npos;
    for (std::size_t i = 0; i < out.full.size(); ++i) {
        char ch = out.full[i];
        if (ch == '<') {
            if (depth == 0) {
                split = i;
                break;
            }
            ++depth;
        } else if (ch == '>') {
            if (depth > 0) {
                --depth;
            }
        }
    }
    if (split == std::string::npos) {
        out.base = trim_copy(out.full);
    } else {
        out.base = trim_copy(out.full.substr(0, split));
        out.suffix = out.full.substr(split);
    }
    out.qualified = out.base.find("::") != std::string::npos;
    return out;
}

std::string strip_leading_colons(std::string_view text) {
    if (text.starts_with("::")) {
        return std::string(text.substr(2));
    }
    return std::string(text);
}

bool is_prefix(const std::vector<std::string> &prefix, const std::vector<std::string> &value) {
    if (prefix.size() > value.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (prefix[i] != value[i]) {
            return false;
        }
    }
    return true;
}

std::string join_namespace(const std::vector<std::string> &parts) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out.append("::");
        out.append(parts[i]);
    }
    return out;
}
} // namespace

bool resolve_free_fixtures(std::vector<TestCaseInfo> &cases, const std::vector<FixtureDeclInfo> &fixtures) {
    struct FixtureLookup {
        std::unordered_map<std::string, std::vector<const FixtureDeclInfo*>> by_base;
        std::unordered_map<std::string, const FixtureDeclInfo*> by_qualified;
    };

    std::unordered_map<std::string, FixtureLookup> lookups;
    for (const auto &fixture : fixtures) {
        auto &lookup = lookups[fixture.tu_filename];
        lookup.by_base[fixture.base_name].push_back(&fixture);
        if (!fixture.qualified_name.empty()) {
            lookup.by_qualified[fixture.qualified_name] = &fixture;
        }
    }

    bool ok = true;
    const auto report = [&](const TestCaseInfo &test, const std::string &message) {
        ok = false;
        log_err("gentest_codegen: {}:{}: {} (test {})\n", test.filename, test.line, message, test.display_name);
    };
    const auto scope_name = [](FixtureScope scope) -> std::string_view {
        switch (scope) {
            case FixtureScope::Local:
                return "local";
            case FixtureScope::Suite:
                return "suite";
            case FixtureScope::Global:
                return "global";
        }
        return "unknown";
    };

    for (auto &test : cases) {
        if (test.free_fixture_types.empty()) {
            continue;
        }
        test.free_fixtures.clear();
        const auto it = lookups.find(test.tu_filename);
        const FixtureLookup *lookup = (it == lookups.end()) ? nullptr : &it->second;
        for (std::size_t fixture_idx = 0; fixture_idx < test.free_fixture_types.size(); ++fixture_idx) {
            const std::string &raw_type = test.free_fixture_types[fixture_idx];
            const std::optional<FixtureScope> required_scope =
                (fixture_idx < test.free_fixture_required_scopes.size()) ? test.free_fixture_required_scopes[fixture_idx] : std::nullopt;
            ParsedFixtureType parsed = parse_fixture_type(raw_type);
            if (parsed.base.empty()) {
                report(test, "empty inferred fixture type from function signature");
                continue;
            }
            const std::string base_no_colons = strip_leading_colons(parsed.base);
            const auto emit_local = [&](const std::string &qualified_name) {
                if (required_scope.has_value()) {
                    report(test,
                           fmt::format(
                               "fixture '{}' is declared as '{}' but resolved as local; ensure the fixture declaration is visible and in "
                               "an ancestor namespace",
                               parsed.full, scope_name(*required_scope)));
                    return;
                }
                FreeFixtureUse use{};
                use.type_name = qualified_name;
                use.scope = FixtureScope::Local;
                test.free_fixtures.push_back(std::move(use));
            };
            const auto emit_declared = [&](const FixtureDeclInfo *decl) {
                if (required_scope.has_value() && decl->scope != *required_scope) {
                    report(test,
                           fmt::format("fixture '{}' declared as '{}' but resolved as '{}' via '{}'", parsed.full,
                                       scope_name(*required_scope), scope_name(decl->scope), decl->qualified_name));
                    return;
                }
                FreeFixtureUse use{};
                use.type_name  = decl->qualified_name + parsed.suffix;
                use.scope      = decl->scope;
                use.suite_name = decl->suite_name;
                test.free_fixtures.push_back(std::move(use));
            };

            if (lookup) {
                if (parsed.qualified) {
                    const auto qit = lookup->by_qualified.find(base_no_colons);
                    if (qit != lookup->by_qualified.end()) {
                        const FixtureDeclInfo *decl = qit->second;
                        if (!is_prefix(decl->namespace_parts, test.namespace_parts)) {
                            report(test, fmt::format("fixture '{}' is not in an ancestor namespace of this test", base_no_colons));
                            continue;
                        }
                        emit_declared(decl);
                        continue;
                    }
                    emit_local(parsed.full);
                    continue;
                }

                const auto bit = lookup->by_base.find(parsed.base);
                const FixtureDeclInfo *best = nullptr;
                if (bit != lookup->by_base.end()) {
                    for (const auto *decl : bit->second) {
                        if (!is_prefix(decl->namespace_parts, test.namespace_parts)) {
                            continue;
                        }
                        if (!best || decl->namespace_parts.size() > best->namespace_parts.size()) {
                            best = decl;
                        }
                    }
                    if (!best) {
                        report(test, fmt::format("fixture '{}' is not in an ancestor namespace of this test", parsed.base));
                        continue;
                    }
                    emit_declared(best);
                    continue;
                }
            }

            if (parsed.qualified) {
                emit_local(parsed.full);
                continue;
            }
            const std::string ns_prefix = join_namespace(test.namespace_parts);
            if (ns_prefix.empty()) {
                emit_local(parsed.base + parsed.suffix);
            } else {
                emit_local(ns_prefix + "::" + parsed.base + parsed.suffix);
            }
        }
    }

    return ok;
}

} // namespace gentest::codegen
