// Helper utilities for discovery: template param collection and validation
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>

#include <clang/AST/Decl.h>

#include "axis_expander.hpp"
#include "validate.hpp"

namespace gentest::codegen::disc {

struct TParam {
    enum class Kind { Type, NTTP };
    Kind        kind;
    std::string name;
};

// Collect function template parameters in declaration order.
// Returns true and fills out on success; returns false if function is not a template.
inline bool collect_template_params(const clang::FunctionDecl& func, std::vector<TParam>& out) {
    const auto* ftd = func.getDescribedFunctionTemplate();
    if (ftd == nullptr) return false;
    const auto* tpl = ftd->getTemplateParameters();
    out.clear();
    out.reserve(tpl->size());
    for (unsigned i = 0; i < tpl->size(); ++i) {
        const clang::NamedDecl* p = tpl->getParam(i);
        if (llvm::isa<clang::TemplateTypeParmDecl>(p)) {
            out.push_back({TParam::Kind::Type, p->getNameAsString()});
        } else if (llvm::isa<clang::NonTypeTemplateParmDecl>(p)) {
            out.push_back({TParam::Kind::NTTP, p->getNameAsString()});
        } else {
            return false; // template-template not supported
        }
    }
    return true;
}

// Validate that attribute-provided sets cover all declared template parameters by name and kind,
// and that no unknown parameter names are present in attributes.
inline bool validate_template_attributes(
    const std::vector<std::pair<std::string, std::vector<std::string>>>& template_sets,
    const std::vector<TParam>&                                           decl_order,
    const std::function<void(const std::string&)>&                       report)
{
#ifdef GENTEST_DISABLE_TEMPLATE_VALIDATION
    (void)template_sets; (void)decl_order; (void)report; return true;
#else
    std::map<std::string, std::vector<std::string>> set_map; for (const auto& p : template_sets) set_map.emplace(p.first, p.second);
    // Require coverage
    for (const auto& tp : decl_order) {
        if (!set_map.contains(tp.name)) { report("missing 'template(" + tp.name + ", ...)' attribute for template parameter '" + tp.name + "'"); return false; }
    }
    // Unknown names in attributes
    for (const auto& kv : set_map) {
        bool known = false; for (const auto& tp : decl_order) { if (tp.name == kv.first) { known = true; break; } }
        if (!known) { report("unknown template parameter '" + kv.first + "' in attributes"); return false; }
    }
    return true;
#endif
}

// Build ordered template argument combinations in declaration order.
inline std::vector<std::vector<std::string>> build_template_arg_combos(
    const std::vector<std::pair<std::string, std::vector<std::string>>>& template_sets,
    const std::vector<TParam>&                                           decl_order)
{
    std::map<std::string, std::vector<std::string>> set_map; for (const auto& p : template_sets) set_map.emplace(p.first, p.second);
    std::vector<std::vector<std::string>> axes;
    axes.reserve(decl_order.size());
    for (const auto& tp : decl_order) {
        axes.push_back(set_map[tp.name]);
    }
    return gentest::codegen::util::cartesian(axes);
}

// Fallback: build combinations by attribute order (types first, then NTTPs).
inline std::vector<std::vector<std::string>> build_template_arg_combos_attr_order(
    const std::vector<std::pair<std::string, std::vector<std::string>>>& template_sets)
{
    std::vector<std::vector<std::string>> axes;
    axes.reserve(template_sets.size());
    for (const auto& p : template_sets) axes.push_back(p.second);
    return gentest::codegen::util::cartesian(axes);
}

} // namespace gentest::codegen::disc

namespace gentest::codegen::disc {

// Build Cartesian product for value parameter axes and collect their type names in order.
inline std::vector<std::vector<std::string>> build_value_arg_combos(
    const std::vector<AttributeSummary::ParamSet>& param_sets,
    std::vector<std::string>&                      out_type_names) {
    out_type_names.clear();
    out_type_names.reserve(param_sets.size());
    std::vector<std::vector<std::string>> axes; axes.reserve(param_sets.size());
    for (const auto& ps : param_sets) { axes.push_back(ps.values); out_type_names.push_back(ps.type_name); }
    return gentest::codegen::util::cartesian(axes);
}

// Pack combos are not a pure Cartesian product: each row contributes a tuple of
// (value arguments, type names). This helper flattens them into a list of
// combinations that can be concatenated with scalar value combos.
struct PackCombo { std::vector<std::string> args; std::vector<std::string> types; };

inline std::vector<PackCombo> build_pack_arg_combos(const std::vector<AttributeSummary::ParamPack>& packs) {
    std::vector<PackCombo> combos{{}}; // start with empty (args, types)
    for (const auto& pp : packs) {
        std::vector<PackCombo> next;
        next.reserve(combos.size() * pp.rows.size());
        for (const auto& partial : combos) {
            for (const auto& row : pp.rows) {
                PackCombo pc = partial;
                pc.args.insert(pc.args.end(), row.begin(), row.end());
                pc.types.insert(pc.types.end(), pp.types.begin(), pp.types.end());
                next.push_back(std::move(pc));
            }
        }
        combos = std::move(next);
    }
    if (combos.empty()) combos.push_back({{}, {}});
    return combos;
}

} // namespace gentest::codegen::disc
