// Helper utilities for discovery: template param collection and validation
#pragma once

#include "axis_expander.hpp"
#include "validate.hpp"

#include <cctype>
#include <clang/AST/Decl.h>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::codegen::disc {

inline std::string trim_ascii_copy(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

inline char previous_non_space(std::string_view text) {
    for (std::size_t idx = text.size(); idx > 0; --idx) {
        const char ch = text[idx - 1];
        if (std::isspace(static_cast<unsigned char>(ch)) == 0) {
            return ch;
        }
    }
    return '\0';
}

inline char next_non_space(std::string_view text, std::size_t index) {
    while (index < text.size()) {
        const char ch = text[index];
        if (std::isspace(static_cast<unsigned char>(ch)) == 0) {
            return ch;
        }
        ++index;
    }
    return '\0';
}

inline bool is_likely_template_left(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == ':' || ch == '>' || ch == ')' || ch == ']';
}

inline bool is_likely_template_right(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == ':' || ch == '(' || ch == '+' || ch == '-' || ch == '\'';
}

inline bool is_word_char(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_'; }

inline bool is_char_literal_prefix(std::string_view text, std::size_t index) {
    if (index >= 1 && (text[index - 1] == 'L' || text[index - 1] == 'u' || text[index - 1] == 'U')) {
        return index == 1 || !is_word_char(text[index - 2]);
    }
    if (index >= 2 && text[index - 2] == 'u' && text[index - 1] == '8') {
        return index == 2 || !is_word_char(text[index - 3]);
    }
    return false;
}

inline bool should_enter_char_literal(std::string_view text, std::size_t index) {
    if (index >= text.size() || text[index] != '\'') {
        return false;
    }
    if (is_char_literal_prefix(text, index)) {
        return true;
    }

    const char prev = index > 0 ? text[index - 1] : '\0';
    const char next = index + 1 < text.size() ? text[index + 1] : '\0';
    if (next == '\0') {
        return false;
    }
    if (std::isalnum(static_cast<unsigned char>(prev)) != 0 && std::isalnum(static_cast<unsigned char>(next)) != 0) {
        return false;
    }
    return true;
}

inline std::string next_identifier_token(std::string_view text, std::size_t index) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
    }
    if (index >= text.size()) {
        return {};
    }
    const char first = text[index];
    if (std::isalpha(static_cast<unsigned char>(first)) == 0 && first != '_') {
        return {};
    }

    std::size_t end = index + 1;
    while (end < text.size()) {
        const char ch = text[end];
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
            ++end;
            continue;
        }
        break;
    }
    return std::string(text.substr(index, end - index));
}

inline bool has_matching_angle_close(std::string_view text, std::size_t open_index) {
    int  depth        = 0;
    int  nested_angle = 0;
    bool in_string    = false;
    bool in_char      = false;
    bool escape_next  = false;

    for (std::size_t idx = open_index + 1; idx < text.size(); ++idx) {
        const char ch = text[idx];
        if (in_string) {
            if (escape_next) {
                escape_next = false;
            } else if (ch == '\\') {
                escape_next = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (in_char) {
            if (escape_next) {
                escape_next = false;
            } else if (ch == '\\') {
                escape_next = true;
            } else if (ch == '\'') {
                in_char = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '\'' && should_enter_char_literal(text, idx)) {
            in_char = true;
            continue;
        }

        if (ch == '(' || ch == '[' || ch == '{') {
            ++depth;
            continue;
        }
        if (ch == ')' || ch == ']' || ch == '}') {
            if (depth > 0) {
                --depth;
            }
            continue;
        }
        if (depth > 0) {
            continue;
        }

        if (ch == '<') {
            ++nested_angle;
            continue;
        }
        if (ch == '>') {
            if (nested_angle == 0) {
                const char follower = next_non_space(text, idx + 1);
                if (follower == '\0') {
                    return true;
                }
                switch (follower) {
                case '_':
                case ',':
                case ')':
                case ']':
                case '}':
                case '{':
                case '(':
                case ':':
                case ';':
                case '*':
                case '&': return true;
                default: {
                    const std::string token = next_identifier_token(text, idx + 1);
                    if (token == "const" || token == "volatile") {
                        return true;
                    }
                    if (token.empty()) {
                        return false;
                    }
                    std::size_t token_start = idx + 1;
                    while (token_start < text.size() && std::isspace(static_cast<unsigned char>(text[token_start])) != 0) {
                        ++token_start;
                    }
                    const std::size_t token_end = token_start + token.size();
                    const char        after     = next_non_space(text, token_end);
                    return after == '{';
                }
                }
            }
            --nested_angle;
        }
    }
    return false;
}

struct SplitTopLevelItemsResult {
    std::vector<std::string> parts;
    bool                     had_empty_item = false;
};

inline SplitTopLevelItemsResult split_top_level_items_result(std::string_view text) {
    SplitTopLevelItemsResult result;
    std::string              current;
    int                      depth       = 0;
    int                      angle_depth = 0;
    bool                     in_string   = false;
    bool                     in_char     = false;
    bool                     escape_next = false;

    auto should_open_angle = [&](std::size_t idx) {
        const char prev = previous_non_space(current);
        if (prev == '\0' || !is_likely_template_left(prev)) {
            return false;
        }
        const char next = next_non_space(text, idx + 1);
        if (next == '\0' || next == '<' || next == '=' || !is_likely_template_right(next)) {
            return false;
        }
        return has_matching_angle_close(text, idx);
    };

    for (std::size_t idx = 0; idx < text.size(); ++idx) {
        const char ch = text[idx];
        if (in_string) {
            current.push_back(ch);
            if (escape_next) {
                escape_next = false;
            } else if (ch == '\\') {
                escape_next = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (in_char) {
            current.push_back(ch);
            if (escape_next) {
                escape_next = false;
            } else if (ch == '\\') {
                escape_next = true;
            } else if (ch == '\'') {
                in_char = false;
            }
            continue;
        }

        switch (ch) {
        case '"':
            in_string = true;
            current.push_back(ch);
            break;
        case '\'':
            if (should_enter_char_literal(text, idx)) {
                in_char = true;
            }
            current.push_back(ch);
            break;
        case '(':
        case '[':
        case '{':
            ++depth;
            current.push_back(ch);
            break;
        case ')':
        case ']':
        case '}':
            if (depth > 0) {
                --depth;
            }
            current.push_back(ch);
            break;
        case '<':
            if (should_open_angle(idx)) {
                ++angle_depth;
            }
            current.push_back(ch);
            break;
        case '>':
            if (angle_depth > 0) {
                --angle_depth;
            }
            current.push_back(ch);
            break;
        case ',':
            if (depth == 0 && angle_depth == 0) {
                const std::string token = trim_ascii_copy(current);
                if (token.empty()) {
                    result.had_empty_item = true;
                } else {
                    result.parts.push_back(token);
                }
                current.clear();
                break;
            }
            [[fallthrough]];
        default: current.push_back(ch); break;
        }
    }

    const std::string token = trim_ascii_copy(current);
    if (token.empty()) {
        if (!text.empty() && text.back() == ',') {
            result.had_empty_item = true;
        }
    } else {
        result.parts.push_back(token);
    }
    return result;
}

inline std::vector<std::string> split_top_level_items(std::string_view text) { return split_top_level_items_result(text).parts; }

struct ParsedParenthesizedRow {
    std::vector<std::string> parts;
    bool                     had_empty_item = false;
};

inline ParsedParenthesizedRow parse_parenthesized_row(std::string_view text) {
    const std::string trimmed = trim_ascii_copy(text);
    if (trimmed.size() < 2 || trimmed.front() != '(' || trimmed.back() != ')') {
        return ParsedParenthesizedRow{
            .parts = {trimmed},
        };
    }

    const auto split = split_top_level_items_result(std::string_view(trimmed).substr(1, trimmed.size() - 2));
    return ParsedParenthesizedRow{
        .parts          = split.parts,
        .had_empty_item = split.had_empty_item,
    };
}

inline bool is_parenthesized_row(std::string_view text) {
    const std::string trimmed = trim_ascii_copy(text);
    return trimmed.size() >= 2 && trimmed.front() == '(' && trimmed.back() == ')';
}

inline auto make_template_param_info(const clang::NamedDecl *param) -> TemplateParamInfo {
    TemplateParamInfo info{};
    info.name = param->getNameAsString();
    if (const auto *ttp = llvm::dyn_cast<clang::TemplateTypeParmDecl>(param)) {
        info.kind    = TemplateParamKind::Type;
        info.is_pack = ttp->isParameterPack();
    } else if (const auto *nttp = llvm::dyn_cast<clang::NonTypeTemplateParmDecl>(param)) {
        info.kind    = TemplateParamKind::Value;
        info.is_pack = nttp->isParameterPack();
    } else {
        info.kind    = TemplateParamKind::Template;
        info.is_pack = llvm::cast<clang::TemplateTemplateParmDecl>(param)->isParameterPack();
    }
    info.usage_spelling = info.name;
    if (info.is_pack) {
        info.usage_spelling += "...";
    }
    return info;
}

// Collect function template parameters in declaration order.
// Returns true and fills out on success; returns false if function is not a template.
inline bool collect_template_params(const clang::FunctionDecl &func, std::vector<TemplateParamInfo> &out) {
    const auto *ftd = func.getDescribedFunctionTemplate();
    if (ftd == nullptr) {
        return false;
    }
    const auto *tpl = ftd->getTemplateParameters();
    out.clear();
    out.reserve(tpl->size());
    for (unsigned i = 0; i < tpl->size(); ++i) {
        out.push_back(make_template_param_info(tpl->getParam(i)));
    }
    return true;
}

inline bool validate_template_binding_shape(const TemplateBindingSet &set, const TemplateParamInfo &param,
                                            const std::function<void(const std::string &)> &report) {
    for (const auto &candidate : set.candidates) {
        const bool parenthesized = is_parenthesized_row(candidate);
        if (param.is_pack && !parenthesized) {
            report("template parameter pack '" + param.name + "' requires parenthesized rows in 'template(" + param.name + ", ...)'");
            return false;
        }
        if (!param.is_pack && parenthesized) {
            if (param.kind == TemplateParamKind::Value) {
                continue;
            }
            report("non-pack template parameter '" + param.name + "' does not accept parenthesized rows in 'template(" + param.name +
                   ", ...)'");
            return false;
        }
        if (param.is_pack) {
            const auto parsed = parse_parenthesized_row(candidate);
            if (parsed.had_empty_item) {
                report("template parameter pack '" + param.name + "' does not accept empty row entries in 'template(" + param.name +
                       ", ...)'");
                return false;
            }
        }
    }
    return true;
}

inline std::vector<std::vector<std::string>> build_binding_rows(const TemplateBindingSet &set, bool pack) {
    std::vector<std::vector<std::string>> rows;
    rows.reserve(set.candidates.size());
    for (const auto &candidate : set.candidates) {
        if (pack) {
            rows.push_back(parse_parenthesized_row(candidate).parts);
        } else {
            rows.push_back({trim_ascii_copy(candidate)});
        }
    }
    return rows;
}

inline std::vector<std::vector<std::string>> build_binding_rows_attr_order(const TemplateBindingSet &set) {
    std::vector<std::vector<std::string>> rows;
    rows.reserve(set.candidates.size());
    for (const auto &candidate : set.candidates) {
        if (is_parenthesized_row(candidate)) {
            rows.push_back(parse_parenthesized_row(candidate).parts);
        } else {
            rows.push_back({trim_ascii_copy(candidate)});
        }
    }
    return rows;
}

inline std::vector<std::vector<std::string>> flatten_row_cartesian(const std::vector<std::vector<std::vector<std::string>>> &axes) {
    std::vector<std::vector<std::string>> out;
    if (axes.empty()) {
        out.emplace_back();
        return out;
    }

    out.emplace_back();
    for (const auto &axis : axes) {
        std::vector<std::vector<std::string>> next;
        next.reserve(out.size() * axis.size());
        for (const auto &acc : out) {
            for (const auto &row : axis) {
                auto merged = acc;
                merged.insert(merged.end(), row.begin(), row.end());
                next.push_back(std::move(merged));
            }
        }
        out = std::move(next);
    }

    if (out.empty()) {
        out.emplace_back();
    }
    return out;
}

// Validate that attribute-provided sets cover all declared template parameters by name and shape,
// and that no unknown parameter names are present in attributes.
inline bool validate_template_attributes(const std::vector<TemplateBindingSet>          &template_sets,
                                         const std::vector<TemplateParamInfo>           &decl_order,
                                         const std::function<void(const std::string &)> &report) {
#ifdef GENTEST_DISABLE_TEMPLATE_VALIDATION
    (void)template_sets;
    (void)decl_order;
    (void)report;
    return true;
#else
    std::map<std::string, const TemplateBindingSet *> set_map;
    for (const auto &set : template_sets) {
        set_map.emplace(set.param_name, &set);
    }

    for (const auto &tp : decl_order) {
        const auto it = set_map.find(tp.name);
        if (it == set_map.end()) {
            report("missing 'template(" + tp.name + ", ...)' attribute for template parameter '" + tp.name + "'");
            return false;
        }
        if (!validate_template_binding_shape(*it->second, tp, report)) {
            return false;
        }
    }

    for (const auto &[name, _] : set_map) {
        bool known = false;
        for (const auto &tp : decl_order) {
            if (tp.name == name) {
                known = true;
                break;
            }
        }
        if (!known) {
            report("unknown template parameter '" + name + "' in attributes");
            return false;
        }
    }
    return true;
#endif
}

// Build ordered template argument combinations in declaration order.
inline std::vector<std::vector<std::string>> build_template_arg_combos(const std::vector<TemplateBindingSet> &template_sets,
                                                                       const std::vector<TemplateParamInfo>  &decl_order) {
    std::map<std::string, const TemplateBindingSet *> set_map;
    for (const auto &set : template_sets) {
        set_map.emplace(set.param_name, &set);
    }

    std::vector<std::vector<std::vector<std::string>>> axes;
    axes.reserve(decl_order.size());
    for (const auto &tp : decl_order) {
        axes.push_back(build_binding_rows(*set_map.at(tp.name), tp.is_pack));
    }
    return flatten_row_cartesian(axes);
}

// Fallback: build combinations by attribute order.
inline std::vector<std::vector<std::string>> build_template_arg_combos_attr_order(const std::vector<TemplateBindingSet> &template_sets) {
    std::vector<std::vector<std::vector<std::string>>> axes;
    axes.reserve(template_sets.size());
    for (const auto &set : template_sets) {
        axes.push_back(build_binding_rows_attr_order(set));
    }
    return flatten_row_cartesian(axes);
}

} // namespace gentest::codegen::disc
