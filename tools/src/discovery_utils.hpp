// Helper utilities for discovery: template param collection and validation
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>

#include <clang/AST/Decl.h>

#include "axis_expander.hpp"

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
    const std::vector<std::pair<std::string, std::vector<std::string>>>& type_sets,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& nttp_sets,
    const std::vector<TParam>& decl_order,
    const std::function<void(const std::string&)>& report)
{
#ifdef GENTEST_DISABLE_TEMPLATE_VALIDATION
    (void)type_sets; (void)nttp_sets; (void)decl_order; (void)report; return true;
#else
    std::map<std::string, std::vector<std::string>> type_map; for (const auto& p : type_sets) type_map.emplace(p.first, p.second);
    std::map<std::string, std::vector<std::string>> nttp_map; for (const auto& p : nttp_sets) nttp_map.emplace(p.first, p.second);
    // Require coverage
    for (const auto& tp : decl_order) {
        if (tp.kind == TParam::Kind::Type) {
            if (!type_map.contains(tp.name)) { report("missing 'template(" + tp.name + ", ...)' attribute for type parameter '" + tp.name + "'"); return false; }
        } else {
            if (!nttp_map.contains(tp.name)) { report("missing 'template(NTTP: " + tp.name + ", ...)' attribute for non-type parameter '" + tp.name + "'"); return false; }
        }
    }
    // Unknown names in attributes
    for (const auto& kv : type_map) {
        bool known = false; for (const auto& tp : decl_order) { if (tp.kind == TParam::Kind::Type && tp.name == kv.first) { known = true; break; } }
        if (!known) { report("unknown type template parameter '" + kv.first + "' in attributes"); return false; }
    }
    for (const auto& kv : nttp_map) {
        bool known = false; for (const auto& tp : decl_order) { if (tp.kind == TParam::Kind::NTTP && tp.name == kv.first) { known = true; break; } }
        if (!known) { report("unknown NTTP template parameter '" + kv.first + "' in attributes"); return false; }
    }
    return true;
#endif
}

// Build ordered template argument combinations in declaration order.
inline std::vector<std::vector<std::string>> build_template_arg_combos(
    const std::vector<std::pair<std::string, std::vector<std::string>>>& type_sets,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& nttp_sets,
    const std::vector<TParam>& decl_order)
{
    std::map<std::string, std::vector<std::string>> type_map; for (const auto& p : type_sets) type_map.emplace(p.first, p.second);
    std::map<std::string, std::vector<std::string>> nttp_map; for (const auto& p : nttp_sets) nttp_map.emplace(p.first, p.second);
    std::vector<std::vector<std::string>> axes;
    axes.reserve(decl_order.size());
    for (const auto& tp : decl_order) {
        axes.push_back(tp.kind == TParam::Kind::Type ? type_map[tp.name] : nttp_map[tp.name]);
    }
    return gentest::codegen::util::cartesian(axes);
}

// Fallback: build combinations by attribute order (types first, then NTTPs).
inline std::vector<std::vector<std::string>> build_template_arg_combos_attr_order(
    const std::vector<std::pair<std::string, std::vector<std::string>>>& type_sets,
    const std::vector<std::pair<std::string, std::vector<std::string>>>& nttp_sets)
{
    std::vector<std::vector<std::string>> axes;
    axes.reserve(type_sets.size() + nttp_sets.size());
    for (const auto& p : type_sets) axes.push_back(p.second);
    for (const auto& p : nttp_sets) axes.push_back(p.second);
    return gentest::codegen::util::cartesian(axes);
}

} // namespace gentest::codegen::disc

