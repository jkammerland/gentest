#include "render_mocks.hpp"

#include "mock_domain_plan.hpp"
#include "mock_output_paths.hpp"
#include "render.hpp"
#include "scan_utils.hpp"
#include "templates_mocks.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <iterator>
#include <llvm/ADT/StringRef.h>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace {
class RenderBuffer {
  public:
    void reserve(std::size_t size) { buffer_.reserve(size); }

    void append_raw(std::string_view text) { buffer_.append(text.data(), text.data() + text.size()); }

    template <typename... Args> void append(fmt::format_string<Args...> format_string, Args &&...args) {
        fmt::format_to(std::back_inserter(buffer_), format_string, std::forward<Args>(args)...);
    }

    std::string str() const { return fmt::to_string(buffer_); }

  private:
    fmt::memory_buffer buffer_;
};
} // namespace

namespace gentest::codegen::render {
namespace {
using ::gentest::codegen::CollectorOptions;
using ::gentest::codegen::MockClassInfo;
using ::gentest::codegen::MockMethodCvQualifier;
using ::gentest::codegen::MockMethodInfo;
using ::gentest::codegen::MockMethodQualifiers;
using ::gentest::codegen::MockMethodRefQualifier;
using ::gentest::codegen::MockParamInfo;

std::string qualifiers_for(const MockMethodQualifiers &qualifiers) {
    std::string q;
    switch (qualifiers.cv) {
    case MockMethodCvQualifier::Const: q += " const"; break;
    case MockMethodCvQualifier::Volatile: q += " volatile"; break;
    case MockMethodCvQualifier::ConstVolatile: q += " const volatile"; break;
    case MockMethodCvQualifier::None: break;
    }
    switch (qualifiers.ref) {
    case MockMethodRefQualifier::LValue: q += " &"; break;
    case MockMethodRefQualifier::RValue: q += " &&"; break;
    case MockMethodRefQualifier::None: break;
    }
    if (qualifiers.is_noexcept)
        q += " noexcept";
    return q;
}

std::string tidy_exception_escape_suppression(const MockMethodQualifiers &qualifiers) {
    if (qualifiers.is_noexcept) {
        return " // NOLINT(bugprone-exception-escape)";
    }
    return {};
}

std::string ensure_global_qualifiers(std::string value) {
    if (value.starts_with("::")) {
        return value;
    }
    std::size_t pos = value.find("::");
    if (pos != std::string::npos) {
        std::size_t scan = pos;
        while (scan > 0 && std::isspace(static_cast<unsigned char>(value[scan - 1]))) {
            --scan;
        }
        std::size_t insert_pos = scan;
        while (insert_pos > 0 && (std::isalnum(static_cast<unsigned char>(value[insert_pos - 1])) || value[insert_pos - 1] == '_')) {
            --insert_pos;
        }
        if (insert_pos == scan) {
            return value;
        }
        const std::string_view prefix(value.data() + insert_pos, scan - insert_pos);
        if (prefix == "const" || prefix == "volatile" || prefix == "typename" || prefix == "struct" || prefix == "class" ||
            prefix == "enum" || prefix == "signed" || prefix == "unsigned" || prefix == "long" || prefix == "short") {
            return value;
        }
        if (!(insert_pos >= 2 && value[insert_pos - 1] == ':' && value[insert_pos - 2] == ':')) {
            value.insert(insert_pos, "::");
        }
    }
    return value;
}

struct MethodTypeRenderParts {
    std::string return_type;
    std::string parameter_list;
    std::string parameter_types;
    std::string qualifiers;
    std::string declarator;
    std::string signature;
    std::string expectation_push_types;
    std::string pointer_type;
};

// Forward declarations
std::string           argument_list(const MockMethodInfo &method);
MethodTypeRenderParts render_method_type_parts(const MockClassInfo &cls, const MockMethodInfo &method);

std::string_view trim_ascii(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

std::vector<std::string_view> split_top_level_commas(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t                   start  = 0;
    int                           angles = 0;
    int                           parens = 0;
    int                           braces = 0;
    int                           square = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        switch (text[i]) {
        case '<': ++angles; break;
        case '>':
            if (angles > 0) {
                --angles;
            }
            break;
        case '(': ++parens; break;
        case ')':
            if (parens > 0) {
                --parens;
            }
            break;
        case '{': ++braces; break;
        case '}':
            if (braces > 0) {
                --braces;
            }
            break;
        case '[': ++square; break;
        case ']':
            if (square > 0) {
                --square;
            }
            break;
        case ',':
            if (angles == 0 && parens == 0 && braces == 0 && square == 0) {
                parts.push_back(trim_ascii(text.substr(start, i - start)));
                start = i + 1;
            }
            break;
        default: break;
        }
    }
    parts.push_back(trim_ascii(text.substr(start)));
    return parts;
}

std::string strip_default_template_argument(std::string_view clause) {
    int angles = 0;
    int parens = 0;
    int braces = 0;
    int square = 0;
    for (std::size_t i = 0; i < clause.size(); ++i) {
        switch (clause[i]) {
        case '<': ++angles; break;
        case '>':
            if (angles > 0) {
                --angles;
            }
            break;
        case '(': ++parens; break;
        case ')':
            if (parens > 0) {
                --parens;
            }
            break;
        case '{': ++braces; break;
        case '}':
            if (braces > 0) {
                --braces;
            }
            break;
        case '[': ++square; break;
        case ']':
            if (square > 0) {
                --square;
            }
            break;
        case '=':
            if (angles == 0 && parens == 0 && braces == 0 && square == 0) {
                return std::string(trim_ascii(clause.substr(0, i)));
            }
            break;
        default: break;
        }
    }
    return std::string(trim_ascii(clause));
}

std::string template_prefix_without_defaults(std::string_view prefix) {
    const std::size_t open  = prefix.find('<');
    const std::size_t close = prefix.rfind('>');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return std::string(prefix);
    }

    const std::string_view clause_text(prefix.data() + open + 1, close - open - 1);
    const auto             clauses = split_top_level_commas(clause_text);

    std::string out(prefix.substr(0, open + 1));
    for (std::size_t i = 0; i < clauses.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += strip_default_template_argument(clauses[i]);
    }
    out += std::string(prefix.substr(close));
    return out;
}

bool contains_identifier_token(std::string_view text, std::string_view token) {
    if (token.empty()) {
        return false;
    }
    std::size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string_view::npos) {
        const bool        left_ok  = pos == 0 || !(std::isalnum(static_cast<unsigned char>(text[pos - 1])) || text[pos - 1] == '_');
        const std::size_t end      = pos + token.size();
        const bool        right_ok = end >= text.size() || !(std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_');
        if (left_ok && right_ok) {
            return true;
        }
        pos = end;
    }
    return false;
}

bool supports_runtime_template_method_ptr_match(const MockMethodInfo &method, std::string_view pointer_type) {
    if (method.template_prefix.empty()) {
        return false;
    }
    const std::size_t open  = method.template_prefix.find('<');
    const std::size_t close = method.template_prefix.rfind('>');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return false;
    }

    const std::string clause_text(method.template_prefix.substr(open + 1, close - open - 1));
    const auto        clauses = split_top_level_commas(clause_text);
    if (clauses.empty()) {
        return false;
    }

    for (const auto clause : clauses) {
        const auto trimmed = trim_ascii(clause);
        if (!(trimmed.starts_with("typename") || trimmed.starts_with("class"))) {
            return false;
        }
    }

    for (const auto &param : method.template_params) {
        if (!contains_identifier_token(pointer_type, param.name)) {
            return false;
        }
    }
    return true;
}

bool pointer_type_depends_on_template_params(const MockMethodInfo &method, std::string_view pointer_type) {
    if (method.template_prefix.empty()) {
        return false;
    }

    for (const auto &param : method.template_params) {
        if (contains_identifier_token(pointer_type, param.name)) {
            return true;
        }
    }
    return false;
}

std::string template_usage_suffix(const MockMethodInfo &method) {
    if (method.template_params.empty()) {
        return {};
    }
    std::string out = "<";
    for (std::size_t i = 0; i < method.template_params.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += method.template_params[i].usage_spelling;
    }
    out += ">";
    return out;
}

std::string dispatch_block(const std::string &indent, const MockClassInfo &cls, const MockMethodInfo &method, const std::string &fq_type,
                           const std::string &tpl_usage) {
    RenderBuffer      block;
    const auto        type_parts        = render_method_type_parts(cls, method);
    const std::string args              = argument_list(method);
    const bool        returns_value     = method.return_type != "void";
    const std::string fq_method         = fmt::format("::{}", method.qualified_name);
    const std::string fq_method_escaped = escape_string(fq_method);
    const std::string dispatch_args     = args.empty() ? std::string{} : fmt::format(", {}", args);
    const std::string method_constant_ref =
        fmt::format("static_cast<{0}>(&{1}::{2}{3})", type_parts.pointer_type, fq_type, method.method_name, tpl_usage);
    const std::string raw_method_ref = fmt::format("&{0}::{1}{2}", fq_type, method.method_name, tpl_usage);
    block.append("{0}auto token = ::gentest::detail::mocking::method_constant_identity<{1}>();\n", indent, method_constant_ref);
    block.append("{0}auto fallback_token = this->__gentest_state_.identify({1});\n", indent, raw_method_ref);
    block.append("{0}{1}this->__gentest_state_.template dispatch_with_fallback<{2}>(token, fallback_token, \"{3}\"{4});\n", indent,
                 returns_value ? "return " : "", type_parts.return_type, fq_method_escaped, dispatch_args);
    return block.str();
}

std::string join_rendered_parameter_list(const std::vector<MockParamInfo> &params, bool include_names) {
    std::string out;
    out.reserve(params.size() * 16);
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i != 0)
            out += ", ";
        out += ensure_global_qualifiers(params[i].type);
        if (include_names && !params[i].name.empty()) {
            out += ' ';
            out += params[i].name;
        }
    }
    return out;
}

MethodTypeRenderParts render_method_type_parts(const MockMethodInfo &method) {
    MethodTypeRenderParts parts;
    parts.return_type            = ensure_global_qualifiers(method.return_type);
    parts.parameter_list         = join_rendered_parameter_list(method.parameters, true);
    parts.parameter_types        = join_rendered_parameter_list(method.parameters, false);
    parts.qualifiers             = qualifiers_for(method.qualifiers);
    parts.declarator             = fmt::format("{}({}){}", method.method_name, parts.parameter_list, parts.qualifiers);
    parts.signature              = fmt::format("{}({})", parts.return_type, parts.parameter_types);
    parts.expectation_push_types = parts.return_type;
    if (!parts.parameter_types.empty()) {
        parts.expectation_push_types += ", ";
        parts.expectation_push_types += parts.parameter_types;
    }
    return parts;
}

std::string join_parameter_list(const std::vector<MockParamInfo> &params) { return join_rendered_parameter_list(params, true); }

MethodTypeRenderParts render_method_type_parts(const MockClassInfo &cls, const MockMethodInfo &method) {
    MethodTypeRenderParts parts = render_method_type_parts(method);
    if (method.is_static) {
        parts.pointer_type += parts.return_type;
        parts.pointer_type += " (*)(";
        parts.pointer_type += parts.parameter_types;
        parts.pointer_type += ')';
        parts.pointer_type += parts.qualifiers;
        return parts;
    }
    parts.pointer_type += parts.return_type;
    parts.pointer_type += " (";
    parts.pointer_type += "::";
    parts.pointer_type += cls.qualified_name;
    parts.pointer_type += "::*)(";
    parts.pointer_type += parts.parameter_types;
    parts.pointer_type += ')';
    parts.pointer_type += parts.qualifiers;
    return parts;
}

std::string render_method_declaration(std::string_view scope_prefix, const MethodTypeRenderParts &parts) {
    return fmt::format("{} {}{}", parts.return_type, scope_prefix, parts.declarator);
}

std::string split_namespace_and_type(const std::string &qualified, std::string &type_out) {
    auto pos = qualified.rfind("::");
    if (pos == std::string::npos) {
        type_out = qualified;
        return {};
    }
    type_out = qualified.substr(pos + 2);
    return qualified.substr(0, pos);
}

std::string open_namespaces(const std::string &ns) {
    std::string code;
    if (ns.empty())
        return code;
    std::size_t start = 0;
    while (true) {
        auto        pos  = ns.find("::", start);
        std::string part = pos == std::string::npos ? ns.substr(start) : ns.substr(start, pos - start);
        if (!part.empty())
            code += "namespace " + part + " {\n";
        if (pos == std::string::npos)
            break;
        start = pos + 2;
    }
    return code;
}

std::string close_namespaces(const std::string &ns) {
    std::string code;
    if (ns.empty())
        return code;
    std::size_t count = 0;
    std::size_t pos   = 0;
    while (true) {
        ++count;
        pos = ns.find("::", pos);
        if (pos == std::string::npos)
            break;
        pos += 2;
    }
    for (std::size_t i = 0; i < count; ++i)
        code += "}\n";
    return code;
}

std::string forward_original_declaration(const MockClassInfo &cls) {
    std::string       type_name;
    const std::string ns = split_namespace_and_type(cls.qualified_name, type_name);
    RenderBuffer      out;
    out.append_raw(open_namespaces(ns));
    out.append("struct {} {{\n", type_name);
    if (cls.has_virtual_destructor) {
        out.append("    virtual ~{}() {{}}\n", type_name);
    }
    for (const auto &m : cls.methods) {
        const auto type_parts = render_method_type_parts(m);
        if (!m.template_prefix.empty()) {
            out.append("    {}\n", m.template_prefix);
        }
        const char *virt = m.is_virtual ? "virtual " : "";
        out.append("    {}{};\n", virt, render_method_declaration({}, type_parts));
    }
    out.append_raw("};\n");
    out.append_raw(close_namespaces(ns));
    out.append_raw("\n");
    return out.str();
}

enum class ForwardingMode {
    Borrow,
    Copy,
    Forward,
    Move,
};

struct ForwardingPolicy {
    enum class ValuePassPolicy {
        Copy,
        Move,
    };

    // Default policy keeps current behavior (move from by-value params).
    static constexpr ValuePassPolicy value_pass_policy = ValuePassPolicy::Move;

    [[nodiscard]] ForwardingMode mode_for(const MockParamInfo &param) const {
        switch (param.pass_style) {
        case MockParamInfo::PassStyle::ForwardingRef: return ForwardingMode::Forward;
        case MockParamInfo::PassStyle::LValueRef: return ForwardingMode::Borrow;
        case MockParamInfo::PassStyle::RValueRef: return ForwardingMode::Move;
        case MockParamInfo::PassStyle::Value:
            return value_pass_policy == ValuePassPolicy::Copy ? ForwardingMode::Copy : ForwardingMode::Move;
        }
        return ForwardingMode::Move;
    }

    [[nodiscard]] std::string expr_for(const MockParamInfo &param) const {
        switch (mode_for(param)) {
        case ForwardingMode::Forward: {
            std::string out;
            out.reserve(param.name.size() * 2 + 24);
            out += "static_cast<decltype(";
            out += param.name;
            out += ")&&>(";
            out += param.name;
            out += ')';
            return out;
        }
        case ForwardingMode::Borrow:
        case ForwardingMode::Copy: return param.name;
        case ForwardingMode::Move: break;
        }
        std::string out;
        out.reserve(param.name.size() * 2 + 24);
        out += "static_cast<decltype(";
        out += param.name;
        out += ")&&>(";
        out += param.name;
        out += ')';
        return out;
    }
};

std::string argument_expr(const MockParamInfo &param) {
    static const ForwardingPolicy policy{};
    return policy.expr_for(param);
}

std::string build_method_declaration(const MockClassInfo &cls, const MockMethodInfo &method) {
    RenderBuffer decl;
    const auto   type_parts = render_method_type_parts(method);
    if (!method.template_prefix.empty()) {
        decl.append("{}\n", method.template_prefix);
    }
    decl.append("{}", render_method_declaration({}, type_parts));
    if (cls.derive_for_virtual && method.is_virtual) {
        decl.append_raw(" override");
    }
    if (!method.template_prefix.empty()) {
        // Inline definition for template methods (must be visible to callers)
        const std::string fq_type = fmt::format("::{}", cls.qualified_name);
        const std::string tpl_use = template_usage_suffix(method);
        decl.append_raw(" {");
        decl.append_raw(tidy_exception_escape_suppression(method.qualifiers));
        decl.append_raw("\n");
        decl.append_raw(dispatch_block("        ", cls, method, fq_type, tpl_use));
        decl.append_raw("    }");
    } else {
        decl.append_raw(";");
    }
    return decl.str();
}

std::string argument_list(const MockMethodInfo &method) {
    std::string args;
    for (std::size_t i = 0; i < method.parameters.size(); ++i) {
        if (i != 0)
            args += ", ";
        args += argument_expr(method.parameters[i]);
    }
    return args;
}

std::string constructors_block(const MockClassInfo &cls) {
    RenderBuffer block;
    if (cls.has_accessible_default_ctor) {
        block.append_raw("    mock();\n");
    }

    for (const auto &ctor : cls.constructors) {
        if (!ctor.template_prefix.empty()) {
            block.append("    {}\n", ctor.template_prefix);
        }
        if (ctor.is_explicit) {
            block.append_raw("    explicit mock(");
        } else {
            block.append_raw("    mock(");
        }
        block.append("{}", join_parameter_list(ctor.parameters));
        block.append_raw(")");
        if (ctor.is_noexcept) {
            block.append_raw(" noexcept");
        }
        block.append_raw(";\n");
    }

    block.append_raw("    ~mock()");
    if (cls.has_virtual_destructor && cls.derive_for_virtual) {
        block.append_raw(" override");
    }
    block.append_raw(";\n");
    return block.str();
}

std::string method_declarations_block(const MockClassInfo &cls) {
    RenderBuffer block;
    for (const auto &method : cls.methods) {
        block.append_raw("    ");
        block.append_raw(build_method_declaration(cls, method));
        block.append_raw("\n");
    }
    return block.str();
}

std::string build_class_declaration(const MockClassInfo &cls) {
    RenderBuffer      header;
    const std::string fq_type = fmt::format("::{}", cls.qualified_name);
    header.append("template <>\nstruct mock<{}>", fq_type);
    if (cls.derive_for_virtual) {
        header.append(" final : public {}", fq_type);
    }
    header.append_raw(" {\n");
    header.append("    using GentestTarget = {};\n", fq_type);
    header.append_raw(constructors_block(cls));
    if (!cls.methods.empty()) {
        header.append_raw(method_declarations_block(cls));
    }
    header.append_raw("\n");
    header.append_raw("  private:\n");
    header.append("    friend struct detail::MockAccess<mock<{}>>;\n", fq_type);
    header.append_raw("    mutable detail::mocking::InstanceState __gentest_state_;\n");
    header.append_raw("};\n\n");
    return header.str();
}

std::string build_mock_access(const MockClassInfo &cls, bool module_mode = false) {
    RenderBuffer      body;
    const std::string fq_type          = fmt::format("::{}", cls.qualified_name);
    const char       *false_type_name  = module_mode ? "::gentest::detail::mocking::FalseType" : "std::false_type";
    const char       *true_type_name   = module_mode ? "::gentest::detail::mocking::TrueType" : "std::true_type";
    const char       *string_view_name = module_mode ? "::gentest::detail::mocking::StringView" : "std::string_view";
    const char       *string_name      = module_mode ? "::gentest::detail::mocking::String" : "std::string";
    const char       *size_type_name   = module_mode ? "::gentest::detail::mocking::SizeType" : "std::size_t";
    body.append("template <>\nstruct MockAccess<mock<{}>> {{\n", fq_type);
    std::vector<std::pair<std::string, std::string>> template_pointer_matchers;
    for (std::size_t i = 0; i < cls.methods.size(); ++i) {
        const auto        &method       = cls.methods[i];
        const auto         type_parts   = render_method_type_parts(cls, method);
        const std::string &pointer_type = type_parts.pointer_type;
        if (method.template_prefix.empty() || !supports_runtime_template_method_ptr_match(method, pointer_type)) {
            continue;
        }
        const std::string matcher_name = fmt::format("method_ptr_matches_template_{}", i);
        body.append("    template <class MethodPtr> struct {} : {} {{}};\n", matcher_name, false_type_name);
        body.append("    {}\n", template_prefix_without_defaults(method.template_prefix));
        body.append("    struct {}<{}> : {} {{}};\n\n", matcher_name, pointer_type, true_type_name);
        template_pointer_matchers.emplace_back(matcher_name, pointer_type);
    }
    body.append_raw("    template <auto Method>\n");
    body.append("    static auto expect_constant(mock<{0}> &instance, {1} method_name) {{\n", fq_type, string_view_name);
    body.append_raw("        using ::gentest::detail::mocking::ExpectationHandle;\n");
    body.append_raw("        using ::gentest::detail::mocking::MethodTraits;\n");
    body.append_raw("        using Signature = typename MethodTraits<decltype(Method)>::Signature;\n");
    body.append_raw("        auto token = ::gentest::detail::mocking::method_constant_identity<Method>();\n");
    body.append("        auto expectation = ::gentest::detail::mocking::ExpectationPusher<Signature>::push(instance.__gentest_state_, "
                "token, {0}(method_name));\n",
                string_name);
    body.append("        return ExpectationHandle<Signature>{{expectation, {0}(method_name)}};\n", string_name);
    body.append_raw("    }\n");
    body.append_raw("\n");
    body.append_raw("    template <class MethodPtr>\n");
    body.append("    static auto expect(mock<{0}> &instance, MethodPtr method) {{\n", fq_type);
    body.append_raw("        using ::gentest::detail::mocking::ExpectationHandle;\n");
    body.append_raw("        using ::gentest::detail::mocking::MethodTraits;\n");
    body.append_raw("        using Signature = typename MethodTraits<MethodPtr>::Signature;\n");
    std::unordered_map<std::string, std::size_t> pointer_type_counts;
    for (const auto &method : cls.methods) {
        ++pointer_type_counts[render_method_type_parts(cls, method).pointer_type];
    }
    std::vector<std::string> compatibility_checks;
    compatibility_checks.reserve(cls.methods.size() + template_pointer_matchers.size());
    for (const auto &method : cls.methods) {
        const auto         type_parts   = render_method_type_parts(cls, method);
        const std::string &pointer_type = type_parts.pointer_type;
        if (!method.template_prefix.empty()) {
            if (!supports_runtime_template_method_ptr_match(method, pointer_type) &&
                !pointer_type_depends_on_template_params(method, pointer_type)) {
                compatibility_checks.push_back(fmt::format("(::gentest::detail::mocking::same_v<MethodPtr, {}> ? 1u : 0u)", pointer_type));
            }
            continue;
        }
        compatibility_checks.push_back(fmt::format("(::gentest::detail::mocking::same_v<MethodPtr, {}> ? 1u : 0u)", pointer_type));
    }
    for (const auto &[matcher_name, _] : template_pointer_matchers) {
        compatibility_checks.push_back(fmt::format("({}<MethodPtr>::value ? 1u : 0u)", matcher_name));
    }
    if (!compatibility_checks.empty()) {
        std::string compatibility_expr;
        for (std::size_t i = 0; i < compatibility_checks.size(); ++i) {
            if (i != 0) {
                compatibility_expr += "\n            + ";
            }
            compatibility_expr += compatibility_checks[i];
        }
        body.append_raw("#if defined(_MSC_VER)\n");
        body.append("        constexpr {} compatible_method_count = {};\n", size_type_name, compatibility_expr);
        body.append_raw("        if constexpr (compatible_method_count > 1) {\n");
        body.append("            ::gentest::detail::record_failure(\"ambiguous mock method pointer for {0}; use EXPECT_CALL(...) or "
                    "gentest::expect<&T::method>(mock, \\\"name\\\") to select the exact method\");\n",
                    escape_string(fq_type));
        body.append_raw("            return ExpectationHandle<Signature>{};\n");
        body.append_raw("        }\n");
        body.append_raw("#endif\n");
    }
    bool first_branch = true;
    for (const auto &method : cls.methods) {
        if (!method.template_prefix.empty()) {
            continue; // rely on generic fallback for template member functions
        }
        const auto         type_parts   = render_method_type_parts(cls, method);
        const std::string &pointer_type = type_parts.pointer_type;
        if (pointer_type_counts[pointer_type] != 1) {
            continue; // avoid ambiguous runtime member-pointer equality matches
        }
        const std::string fq_method           = fmt::format("::{}", method.qualified_name);
        const std::string fq_method_escaped   = escape_string(fq_method);
        const std::string method_constant_ref = fmt::format("static_cast<{0}>(&{1}::{2})", pointer_type, fq_type, method.method_name);
        const std::string branch_intro        = first_branch ? "        if constexpr" : "        else if constexpr";
        first_branch                          = false;
        body.append("{0} (::gentest::detail::mocking::same_v<MethodPtr, {1}>) {{\n", branch_intro, pointer_type);
        body.append("            if (method == static_cast<MethodPtr>(&{0}::{1})) {{\n", fq_type, method.method_name);
        body.append("                auto token = ::gentest::detail::mocking::method_constant_identity<{3}>();\n"
                    "                auto expectation = instance.__gentest_state_.template push_expectation<{2}>(token, \"{4}\");\n"
                    "                return ExpectationHandle<{5}>{{expectation, \"{4}\"}};\n",
                    fq_type, method.method_name, type_parts.expectation_push_types, method_constant_ref, fq_method_escaped,
                    type_parts.signature);
        body.append_raw("            }\n");
        body.append_raw("        }\n");
    }
    body.append_raw("        auto token = instance.__gentest_state_.identify(method);\n");
    body.append_raw("        auto expectation = ::gentest::detail::mocking::ExpectationPusher<Signature>::push(instance.__gentest_state_, "
                    "token, \"(mock method)\");\n");
    body.append_raw("        return ExpectationHandle<Signature>{expectation, \"(mock method)\"};\n");
    body.append_raw("    }\n");
    body.append_raw("\n");
    body.append("    static void set_nice(mock<{0}> &instance, bool v) {{ instance.__gentest_state_.set_nice(v); }}\n", fq_type);
    body.append_raw("};\n\n");
    return body.str();
}

std::string method_definition(const MockClassInfo &cls, const MockMethodInfo &method) {
    RenderBuffer      def;
    const std::string fq_type    = fmt::format("::{}", cls.qualified_name);
    const auto        type_parts = render_method_type_parts(method);
    if (!method.template_prefix.empty()) {
        def.append("{}\n", method.template_prefix);
    }
    def.append("{} {{", render_method_declaration(fmt::format("gentest::mock<{}>::", fq_type), type_parts));
    def.append_raw(tidy_exception_escape_suppression(method.qualifiers));
    def.append_raw("\n");
    const std::string tpl_usage = template_usage_suffix(method);
    def.append_raw(dispatch_block("    ", cls, method, fq_type, tpl_usage));
    def.append_raw("}\n");
    return def.str();
}

struct DefinitionIncludeBlock {
    std::string text;
    std::string error;
};

struct SourcePathAlias {
    std::filesystem::path canonical_prefix;
    std::filesystem::path visible_prefix;
};

[[nodiscard]] std::filesystem::path normalize_absolute_lexical(std::filesystem::path path) {
    std::error_code ec;
    if (path.is_relative()) {
        const std::filesystem::path abs = std::filesystem::absolute(path, ec);
        if (!ec) {
            path = abs;
        }
    }
    return path.lexically_normal();
}

[[nodiscard]] std::vector<SourcePathAlias> build_source_path_aliases(const CollectorOptions &options) {
    namespace fs = std::filesystem;
    std::vector<SourcePathAlias> aliases;
    std::error_code              ec;

    for (const auto &source : options.sources) {
        fs::path visible = normalize_absolute_lexical(source);
        ec.clear();
        const fs::path canonical = fs::weakly_canonical(visible, ec).lexically_normal();
        if (ec || canonical.empty() || canonical == visible) {
            ec.clear();
            continue;
        }

        fs::path visible_cursor   = visible;
        fs::path canonical_cursor = canonical;
        while (!visible_cursor.empty() && !canonical_cursor.empty()) {
            if (visible_cursor == canonical_cursor) {
                break;
            }
            aliases.push_back(SourcePathAlias{
                .canonical_prefix = canonical_cursor,
                .visible_prefix   = visible_cursor,
            });
            if (!visible_cursor.has_parent_path() || !canonical_cursor.has_parent_path()) {
                break;
            }
            visible_cursor   = visible_cursor.parent_path();
            canonical_cursor = canonical_cursor.parent_path();
        }
    }

    std::ranges::sort(aliases, [](const SourcePathAlias &lhs, const SourcePathAlias &rhs) {
        return lhs.canonical_prefix.generic_string().size() > rhs.canonical_prefix.generic_string().size();
    });
    const auto unique_tail = std::ranges::unique(aliases, [](const SourcePathAlias &lhs, const SourcePathAlias &rhs) {
        return lhs.canonical_prefix == rhs.canonical_prefix && lhs.visible_prefix == rhs.visible_prefix;
    });
    aliases.erase(unique_tail.begin(), unique_tail.end());
    return aliases;
}

[[nodiscard]] std::filesystem::path remap_to_visible_source_path(const std::filesystem::path        &path,
                                                                 const std::vector<SourcePathAlias> &aliases) {
    for (const auto &alias : aliases) {
        const auto rel = path.lexically_relative(alias.canonical_prefix);
        if (!rel.empty() && !rel.is_absolute()) {
            return (alias.visible_prefix / rel).lexically_normal();
        }
        if (path == alias.canonical_prefix) {
            return alias.visible_prefix;
        }
    }
    return path;
}

DefinitionIncludeBlock build_definition_include_block(const CollectorOptions &options, const std::vector<const MockClassInfo *> &classes,
                                                      const std::vector<SourcePathAlias> &source_aliases) {
    namespace fs = std::filesystem;
    DefinitionIncludeBlock result;
    std::set<std::string>  includes;

    fs::path registry_dir = options.mock_registry_path.parent_path();
    if (registry_dir.empty()) {
        registry_dir = ".";
    }
    std::error_code ec;
    registry_dir = registry_dir.lexically_normal();

    for (const auto *cls : classes) {
        if (!cls) {
            continue;
        }
        if (cls->definition_kind == MockClassInfo::DefinitionKind::NamedModule) {
            // Named modules cannot be recovered with a textual #include. The
            // consumer must make the type visible before injecting the
            // generated mock specializations.
            continue;
        }
        if (cls->definition_file.empty()) {
            result.error = fmt::format("mock renderer: missing definition file for '{}'", cls->qualified_name);
            return result;
        }

        fs::path def_path{cls->definition_file};
        def_path = def_path.lexically_normal();
        def_path = remap_to_visible_source_path(def_path, source_aliases);

        fs::path include_path;
        // Avoid std::filesystem::proximate() here: it canonicalizes through the
        // host filesystem and can rewrite symlink-visible paths into real host
        // paths that are not usable from sandboxed or source-mounted builds.
        fs::path rel_path = def_path.lexically_relative(registry_dir);
        if (!rel_path.empty() && !rel_path.is_absolute()) {
            include_path = std::move(rel_path);
        } else {
            // Cross-root/drive paths can legitimately fail to relativize. In
            // that case, fall back to an absolute include instead of failing.
            include_path = def_path;
        }
        if (include_path.empty()) {
            result.error = fmt::format("mock renderer: unable to resolve include path for '{}' (registry='{}', definition='{}')",
                                       cls->qualified_name, registry_dir.generic_string(), def_path.generic_string());
            return result;
        }
        includes.insert(include_path.generic_string());
    }

    RenderBuffer include_block;
    include_block.reserve(includes.size() * 48);
    for (const auto &path : includes) {
        include_block.append("#include \"{}\"\n", path);
    }
    if (!includes.empty()) {
        include_block.append_raw("\n");
    }
    result.text = include_block.str();
    return result;
}

std::string generate_empty_registry_header(std::string_view label) {
    return fmt::format("#pragma once\n\n// gentest_codegen: no mocks discovered for domain '{}'.\n", label);
}

std::string generate_empty_implementation_header(std::string_view label) {
    return fmt::format(
        "#pragma once\n\nnamespace gentest {{\n// gentest_codegen: no mocks discovered for domain '{}'.\n}} // namespace gentest\n", label);
}

void append_mock_implementation(RenderBuffer &impl, const MockClassInfo &cls) {
    const std::string fq_type = fmt::format("::{}", cls.qualified_name);
    if (cls.has_accessible_default_ctor) {
        // Spell out state initialization so imported-module mocks do not rely
        // on compiler defaulting behavior for InstanceState.
        impl.append("inline mock<{0}>::mock() : __gentest_state_{{}} {{}}\n", fq_type);
    }
    for (const auto &ctor : cls.constructors) {
        if (!ctor.template_prefix.empty()) {
            impl.append("{}\n", template_prefix_without_defaults(ctor.template_prefix));
        }
        impl.append("inline mock<{0}>::mock(", fq_type);
        impl.append("{}", join_parameter_list(ctor.parameters));
        impl.append_raw(")");
        if (ctor.is_noexcept) {
            impl.append_raw(" noexcept");
        }
        if (cls.derive_for_virtual) {
            impl.append_raw(" : ");
            impl.append("{}", fq_type);
            impl.append_raw("(");
            for (std::size_t i = 0; i < ctor.parameters.size(); ++i) {
                if (i != 0)
                    impl.append_raw(", ");
                const auto &p = ctor.parameters[i];
                impl.append("{}", argument_expr(p));
            }
            impl.append_raw("), __gentest_state_{}");
            impl.append_raw(" {}\n");
        } else {
            impl.append_raw(" : __gentest_state_{} {\n");
            for (const auto &p : ctor.parameters) {
                impl.append("    (void){};\n", p.name);
            }
            impl.append_raw("}\n");
        }
        impl.append_raw("\n");
    }
    impl.append("inline mock<{0}>::~mock() {{ this->__gentest_state_.verify_all(); }}\n\n", fq_type);
    for (const auto &method : cls.methods) {
        if (!method.template_prefix.empty())
            continue; // defined inline in class declaration
        std::string def = method_definition(cls, method);
        // Prefix with inline to be ODR-safe if included in multiple TUs
        def.insert(0, "inline ");
        impl.append_raw(def);
        impl.append_raw("\n");
    }
    impl.append_raw("\n");
}

std::string generate_implementation_header(const std::vector<const MockClassInfo *> &mocks) {
    if (mocks.empty()) {
        return generate_empty_implementation_header("empty");
    }

    RenderBuffer impl;
    impl.reserve(mocks.size() * 256);
    impl.append_raw(tpl::mocks::impl_preamble);
    // Note: Include order matters. This header is intended to be included
    // after the test sources (so original types are complete) and after
    // including gentest/mock.h in the including TU.

    for (const auto *cls : mocks) {
        append_mock_implementation(impl, *cls);
    }

    impl.append_raw(tpl::mocks::impl_footer);
    return impl.str();
}

std::string generate_dispatcher_header(const std::filesystem::path &included_file) {
    RenderBuffer out;
    out.append_raw("// This file is auto-generated by gentest_codegen.\n");
    out.append_raw("// Do not edit manually.\n\n");
    out.append_raw("#pragma once\n\n");
    out.append("#include \"{}\"\n\n", included_file.filename().generic_string());
    return out.str();
}

} // namespace

MockRenderResult render_mocks(const CollectorOptions &options, const std::vector<MockClassInfo> &mocks) {
    MockRenderResult result;
    const auto       domains = gentest::codegen::build_mock_output_domains(options);

    std::vector<const MockClassInfo *> classes;
    classes.reserve(mocks.size());
    for (const auto &cls : mocks) {
        classes.push_back(&cls);
    }
    std::ranges::sort(classes, {}, [](const MockClassInfo *cls) { return cls->qualified_name; });

    const auto                                                          source_aliases = build_source_path_aliases(options);
    std::vector<const MockClassInfo *>                                  header_classes;
    std::unordered_map<std::string, std::vector<const MockClassInfo *>> module_classes;
    for (const auto *cls : classes) {
        if (cls->definition_kind == MockClassInfo::DefinitionKind::HeaderLike) {
            header_classes.push_back(cls);
            continue;
        }
        if (cls->definition_kind == MockClassInfo::DefinitionKind::NamedModule) {
            module_classes[cls->definition_module_name].push_back(cls);
        }
    }

    MockOutputs                              out;
    const std::vector<const MockClassInfo *> empty_classes;
    for (const auto &domain : domains) {
        const auto &domain_classes =
            domain.kind == MockOutputDomainPlan::Kind::Header ? header_classes : ([&]() -> const std::vector<const MockClassInfo *> & {
                const auto it = module_classes.find(domain.module_name);
                return it != module_classes.end() ? it->second : empty_classes;
            }());

        const std::string domain_label = domain.kind == MockOutputDomainPlan::Kind::Header ? std::string{"header"} : domain.module_name;

        std::string registry_content;
        if (domain_classes.empty()) {
            registry_content = generate_empty_registry_header(domain_label);
        } else {
            const DefinitionIncludeBlock include_block = build_definition_include_block(options, domain_classes, source_aliases);
            if (!include_block.error.empty()) {
                result.error = include_block.error;
                return result;
            }

            RenderBuffer header;
            header.reserve(domain_classes.size() * 256);
            header.append_raw(tpl::mocks::registry_preamble);
            header.append_raw(include_block.text);
            header.append_raw("namespace gentest {\n\n");
            for (const auto *cls : domain_classes) {
                header.append_raw(build_class_declaration(*cls));
            }
            header.append_raw("namespace detail {\n\n");
            for (const auto *cls : domain_classes) {
                header.append_raw(build_mock_access(*cls));
            }
            header.append_raw("} // namespace detail\n");
            header.append_raw("} // namespace gentest\n");
            registry_content = header.str();
        }

        const std::string impl_content =
            domain_classes.empty() ? generate_empty_implementation_header(domain_label) : generate_implementation_header(domain_classes);

        out.additional_files.push_back(MockGeneratedFile{
            .path    = domain.registry_path,
            .content = std::move(registry_content),
        });
        out.additional_files.push_back(MockGeneratedFile{
            .path    = domain.impl_path,
            .content = impl_content,
        });
    }

    const auto header_domain =
        std::ranges::find_if(domains, [](const MockOutputDomainPlan &domain) { return domain.kind == MockOutputDomainPlan::Kind::Header; });
    if (header_domain == domains.end()) {
        result.error = "mock renderer: internal error: missing header mock domain";
        return result;
    }

    out.registry_header     = generate_dispatcher_header(header_domain->registry_path);
    out.implementation_unit = generate_dispatcher_header(header_domain->impl_path);
    result.outputs          = std::move(out);
    return result;
}

std::string render_module_mock_attachment(const MockClassInfo &mock) {
    RenderBuffer out;
    out.append_raw("\n// gentest_codegen: injected mock attachment.\n");
    out.append_raw("\n");
    out.append_raw("namespace gentest {\n\n");
    out.append_raw(build_class_declaration(mock));
    out.append_raw("namespace detail {\n\n");
    out.append_raw(build_mock_access(mock, true));
    out.append_raw("} // namespace detail\n\n");
    append_mock_implementation(out, mock);
    out.append_raw("} // namespace gentest\n");
    return out.str();
}

} // namespace gentest::codegen::render
