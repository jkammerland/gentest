#include "render_mocks.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fmt/format.h>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace {
template <typename... Args>
inline void append_format(std::string &out, fmt::format_string<Args...> format_string, Args &&...args) {
    fmt::format_to(std::back_inserter(out), format_string, std::forward<Args>(args)...);
}
} // namespace

namespace gentest::codegen::render {
namespace {

using ::gentest::codegen::CollectorOptions;
using ::gentest::codegen::MockClassInfo;
using ::gentest::codegen::MockMethodInfo;
using ::gentest::codegen::MockParamInfo;

std::string qualifiers_for(const MockMethodInfo &method) {
    std::string q;
    if (method.is_const)
        q += " const";
    if (method.is_volatile)
        q += " volatile";
    if (!method.ref_qualifier.empty()) {
        q += ' ';
        q += method.ref_qualifier;
    }
    if (method.is_noexcept)
        q += " noexcept";
    return q;
}

std::string ensure_global_qualifiers(std::string value) {
    if (value.rfind("::", 0) == 0) {
        return value;
    }
    std::size_t pos = value.find("::");
    if (pos != std::string::npos) {
        std::size_t scan = pos;
        while (scan > 0 && std::isspace(static_cast<unsigned char>(value[scan - 1]))) {
            --scan;
        }
        std::size_t insert_pos = scan;
        while (insert_pos > 0 &&
               (std::isalnum(static_cast<unsigned char>(value[insert_pos - 1])) || value[insert_pos - 1] == '_')) {
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

// Forward declarations
std::string argument_list(const MockMethodInfo &method);

std::string dispatch_block(const std::string &indent, const MockMethodInfo &method, const std::string &fq_type,
                           const std::string &tpl_usage) {
    std::string block;
    append_format(block, "{0}auto token = this->__gentest_state_.identify(&{1}::{2}{3});\n", indent, fq_type, method.method_name,
                  tpl_usage);
    const std::string return_type   = ensure_global_qualifiers(method.return_type);
    const std::string args          = argument_list(method);
    const bool        returns_value = method.return_type != "void";
    std::string       fq_method;
    fq_method.reserve(method.qualified_name.size() + 2);
    fq_method += "::";
    fq_method += method.qualified_name;
    std::string dispatch_args;
    if (!args.empty()) {
        dispatch_args.reserve(args.size() + 2);
        dispatch_args += ", ";
        dispatch_args += args;
    }
    append_format(block, "{0}{1}this->__gentest_state_.template dispatch<{2}>(token, \"{3}\"{4});\n", indent,
                  returns_value ? "return " : "", return_type, fq_method, dispatch_args);
    return block;
}

std::string join_parameter_list(const std::vector<MockParamInfo> &params) {
    std::string out;
    out.reserve(params.size() * 16);
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i != 0)
            out += ", ";
        out += ensure_global_qualifiers(params[i].type);
        if (!params[i].name.empty()) {
            out += ' ';
            out += params[i].name;
        }
    }
    return out;
}

std::string join_type_list(const std::vector<MockParamInfo> &params) {
    std::string out;
    out.reserve(params.size() * 12);
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i != 0)
            out += ", ";
        out += ensure_global_qualifiers(params[i].type);
    }
    return out;
}

std::string signature_from(const MockMethodInfo &method) {
    std::string sig = ensure_global_qualifiers(method.return_type);
    sig += '(';
    sig += join_type_list(method.parameters);
    sig += ')';
    return sig;
}

std::string pointer_type_for(const MockClassInfo &cls, const MockMethodInfo &method) {
    std::string ptr;
    if (method.is_static) {
        ptr += ensure_global_qualifiers(method.return_type);
        ptr += " (*)(";
        ptr += join_type_list(method.parameters);
        ptr += ')';
        ptr += qualifiers_for(method);
        return ptr;
    }
    ptr += ensure_global_qualifiers(method.return_type);
    ptr += " (";
    ptr += "::";
    ptr += cls.qualified_name;
    ptr += "::*)(";
    ptr += join_type_list(method.parameters);
    ptr += ')';
    ptr += qualifiers_for(method);
    return ptr;
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
    if (ns.empty()) return code;
    std::size_t start = 0;
    while (true) {
        auto pos = ns.find("::", start);
        std::string part = pos == std::string::npos ? ns.substr(start) : ns.substr(start, pos - start);
        if (!part.empty()) code += "namespace " + part + " {\n";
        if (pos == std::string::npos) break;
        start = pos + 2;
    }
    return code;
}

std::string close_namespaces(const std::string &ns) {
    std::string code;
    if (ns.empty()) return code;
    std::size_t count = 0;
    std::size_t pos   = 0;
    while (true) {
        ++count;
        pos = ns.find("::", pos);
        if (pos == std::string::npos) break;
        pos += 2;
    }
    for (std::size_t i = 0; i < count; ++i) code += "}\n";
    return code;
}

std::string forward_original_declaration(const MockClassInfo &cls) {
    std::string type_name;
    const std::string ns = split_namespace_and_type(cls.qualified_name, type_name);
    std::string out;
    out += open_namespaces(ns);
    out += "struct " + type_name + " {\n";
    if (cls.has_virtual_destructor) {
        out += "    virtual ~" + type_name + "() {}\n";
    }
    for (const auto &m : cls.methods) {
        if (!m.template_prefix.empty()) {
            out += "    " + m.template_prefix + "\n";
        }
        std::string virt = m.is_virtual ? std::string("virtual ") : std::string();
        out += "    " + virt + ensure_global_qualifiers(m.return_type) + " " + m.method_name + "(" + join_parameter_list(m.parameters) + ")" + qualifiers_for(m) + ";\n";
    }
    out += "};\n";
    out += close_namespaces(ns);
    out += "\n";
    return out;
}

std::string argument_expr(const MockParamInfo &param) {
    switch (param.pass_style) {
    case MockParamInfo::PassStyle::ForwardingRef: {
        std::string out;
        out.reserve(param.name.size() * 2 + 26);
        out += "std::forward<decltype(";
        out += param.name;
        out += ")>(";
        out += param.name;
        out += ')';
        return out;
    }
    case MockParamInfo::PassStyle::LValueRef:
        return param.name;
    case MockParamInfo::PassStyle::RValueRef:
    case MockParamInfo::PassStyle::Value:
        break;
    }
    std::string out;
    out.reserve(param.name.size() + 10);
    out += "std::move(";
    out += param.name;
    out += ')';
    return out;
}

std::string build_method_declaration(const MockClassInfo &cls, const MockMethodInfo &method) {
    std::string decl;
    if (!method.template_prefix.empty()) {
        decl += method.template_prefix;
        decl += '\n';
    }
    decl += method.return_type;
    decl += ' ';
    decl += method.method_name;
    decl += '(';
    decl += join_parameter_list(method.parameters);
    decl += ')';
    decl += qualifiers_for(method);
    if (cls.derive_for_virtual && method.is_virtual) {
        decl += " override";
    }
    if (!method.template_prefix.empty()) {
        // Inline definition for template methods (must be visible to callers)
        std::string fq_type;
        fq_type.reserve(cls.qualified_name.size() + 2);
        fq_type += "::";
        fq_type += cls.qualified_name;
        const std::string tpl_use = [&]() {
            if (method.template_param_names.empty()) return std::string{};
            std::string out = "<";
            for (std::size_t i = 0; i < method.template_param_names.size(); ++i) {
                if (i != 0) out += ", ";
                out += method.template_param_names[i];
            }
            out += ">";
            return out;
        }();
        decl += " {\n";
        decl += dispatch_block("        ", method, fq_type, tpl_use);
        decl += "    }";
    } else {
        decl += ';';
    }
    return decl;
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
    std::string block;
    if (cls.has_accessible_default_ctor) {
        block += "    mock();\n";
    }

    for (const auto &ctor : cls.constructors) {
        if (!ctor.template_prefix.empty()) {
            block += "    ";
            block += ctor.template_prefix;
            block += "\n";
        }
        block += "    ";
        if (ctor.is_explicit) {
            block += "explicit ";
        }
        block += "mock(";
        block += join_parameter_list(ctor.parameters);
        block += ')';
        if (ctor.is_noexcept) {
            block += " noexcept";
        }
        block += ";\n";
    }

    block += "    ~mock()";
    if (cls.has_virtual_destructor && cls.derive_for_virtual) {
        block += " override";
    }
    block += ";\n";
    return block;
}

std::string method_declarations_block(const MockClassInfo &cls) {
    std::string block;
    for (const auto &method : cls.methods) {
        block += "    ";
        block += build_method_declaration(cls, method);
        block += "\n";
    }
    return block;
}

std::string build_class_declaration(const MockClassInfo &cls) {
    std::string header;
    std::string fq_type;
    fq_type.reserve(cls.qualified_name.size() + 2);
    fq_type += "::";
    fq_type += cls.qualified_name;
    header += "template <>\nstruct mock<";
    header += fq_type;
    header += '>';
    if (cls.derive_for_virtual) {
        header += " final : public ";
        header += fq_type;
    }
    header += " {\n";
    append_format(header, "    using GentestTarget = {};\n", fq_type);
    header += constructors_block(cls);
    if (!cls.methods.empty()) {
        header += method_declarations_block(cls);
    }
    header += "\n";
    header += "  private:\n";
    append_format(header, "    friend struct detail::MockAccess<mock<{}>>;\n", fq_type);
    header += "    mutable detail::mocking::InstanceState __gentest_state_;\n";
    header += "};\n\n";
    return header;
}

std::string build_mock_access(const MockClassInfo &cls) {
    std::string body;
    std::string fq_type;
    fq_type.reserve(cls.qualified_name.size() + 2);
    fq_type += "::";
    fq_type += cls.qualified_name;
    append_format(body, "template <>\nstruct MockAccess<mock<{}>> {{\n", fq_type);
    body += "    template <class MethodPtr>\n";
    append_format(body, "    static auto expect(mock<{0}> &instance, MethodPtr method) {{\n", fq_type);
    body += "        using ::gentest::detail::mocking::ExpectationHandle;\n";
    body += "        using ::gentest::detail::mocking::MethodTraits;\n";
    bool first_branch = true;
    for (const auto &method : cls.methods) {
        if (!method.template_prefix.empty()) {
            continue; // rely on generic fallback for template member functions
        }
        const std::string pointer_type = pointer_type_for(cls, method);
        const std::string signature    = signature_from(method);
        const std::string push_args    = [&]() {
            std::string args = ensure_global_qualifiers(method.return_type);
            for (const auto &param : method.parameters) {
                args += ", ";
                args += ensure_global_qualifiers(param.type);
            }
            return args;
        }();
        std::string fq_method;
        fq_method.reserve(method.qualified_name.size() + 2);
        fq_method += "::";
        fq_method += method.qualified_name;
        const std::string branch_intro = first_branch ? "        if constexpr" : "        else if constexpr";
        first_branch                   = false;
        append_format(body, "{0} (std::is_same_v<MethodPtr, {1}>) {{\n", branch_intro, pointer_type);
        append_format(body, "            if (method == static_cast<MethodPtr>(&{0}::{1})) {{\n", fq_type, method.method_name);
        append_format(body,
                      "                auto token = instance.__gentest_state_.identify(&{0}::{1});\n"
                      "                auto expectation = instance.__gentest_state_.template push_expectation<{2}>(token, \"{3}\");\n"
                      "                return ExpectationHandle<{4}>{{expectation, \"{3}\"}};\n",
                      fq_type, method.method_name, push_args, fq_method, signature);
        body += "            }\n";
        body += "        }\n";
    }
    body += "        using Signature = typename MethodTraits<MethodPtr>::Signature;\n";
    body += "        auto token = instance.__gentest_state_.identify(method);\n";
    body += "        auto expectation = ::gentest::detail::mocking::ExpectationPusher<Signature>::push(instance.__gentest_state_, token, \"(mock method)\");\n";
    body += "        return ExpectationHandle<Signature>{expectation, \"(mock method)\"};\n";
    body += "    }\n";
    body += "\n";
    append_format(body, "    static void set_nice(mock<{0}> &instance, bool v) {{ instance.__gentest_state_.set_nice(v); }}\n", fq_type);
    body += "};\n\n";
    return body;
}

std::string method_definition(const MockClassInfo &cls, const MockMethodInfo &method) {
    std::string def;
    std::string fq_type;
    fq_type.reserve(cls.qualified_name.size() + 2);
    fq_type += "::";
    fq_type += cls.qualified_name;
    if (!method.template_prefix.empty()) {
        def += method.template_prefix;
        def += '\n';
    }
    def += ensure_global_qualifiers(method.return_type);
    def += " gentest::mock<";
    def += fq_type;
    def += ">::";
    def += method.method_name;
    def += '(';
    def += join_parameter_list(method.parameters);
    def += ')';
    def += qualifiers_for(method);
    def += " {\n";
    const std::string tpl_usage = [&]() -> std::string {
        if (method.template_param_names.empty()) return std::string{};
        std::string out = "<";
        for (std::size_t i = 0; i < method.template_param_names.size(); ++i) {
            if (i != 0) out += ", ";
            out += method.template_param_names[i];
        }
        out += ">";
        return out;
    }();
    def += dispatch_block("    ", method, fq_type, tpl_usage);
    def += "}\n";
    return def;
}

std::string include_sources_block(const CollectorOptions &options, const std::filesystem::path &base_dir) {
    namespace fs = std::filesystem;
    std::string includes;
    includes.reserve(options.sources.size() * 32);
    for (const auto &src : options.sources) {
        fs::path        spath(src);
        std::error_code ec;
        fs::path        rel = fs::proximate(spath, base_dir, ec);
        if (ec)
            rel = spath;
        append_format(includes, "#include \"{}\"\n", rel.generic_string());
    }
    return includes;
}

std::string generate_implementation_header(const std::vector<const MockClassInfo *> &mocks) {
    std::string impl;
    impl.reserve(mocks.size() * 256);
    impl += "// This file is auto-generated by gentest_codegen.\n// Do not edit manually.\n\n";
    impl += "#pragma once\n\n";
    impl += "#include <utility>\n\n";
    // Note: Include order matters. This header is intended to be included
    // after the test sources (so original types are complete) and after
    // including gentest/mock.h in the including TU.
    impl += "namespace gentest {\n\n";

    for (const auto *cls : mocks) {
        std::string fq_type;
        fq_type.reserve(cls->qualified_name.size() + 2);
        append_format(fq_type, "::{}", cls->qualified_name);
        if (cls->has_accessible_default_ctor) {
            append_format(impl, "inline mock<{0}>::mock() = default;\n", cls->qualified_name);
        }
        for (const auto &ctor : cls->constructors) {
            if (!ctor.template_prefix.empty()) {
                impl += ctor.template_prefix;
                impl += '\n';
            }
            append_format(impl, "inline mock<{0}>::mock(", fq_type);
            impl += join_parameter_list(ctor.parameters);
            impl += ')';
            if (ctor.is_noexcept) {
                impl += " noexcept";
            }
            if (cls->derive_for_virtual) {
                impl += " : ";
                impl += fq_type;
                impl += '(';
                for (std::size_t i = 0; i < ctor.parameters.size(); ++i) {
                    if (i != 0)
                        impl += ", ";
                    const auto &p = ctor.parameters[i];
                    append_format(impl, "std::forward<decltype({0})>({0})", p.name);
                }
                impl += ')';
                impl += " {}\n";
            } else {
                impl += " {\n";
                for (const auto &p : ctor.parameters) {
                    append_format(impl, "    (void){};\n", p.name);
                }
                impl += "}\n";
            }
            impl += '\n';
        }
        append_format(impl, "inline mock<{0}>::~mock() {{ this->__gentest_state_.verify_all(); }}\n\n", cls->qualified_name);
        for (const auto &method : cls->methods) {
            if (!method.template_prefix.empty()) continue; // defined inline in class declaration
            std::string def = method_definition(*cls, method);
            // Prefix with inline to be ODR-safe if included in multiple TUs
            def.insert(0, "inline ");
            impl += def;
            impl += '\n';
        }
        impl += '\n';
    }

    impl += "} // namespace gentest\n";
    return impl;
}

} // namespace

std::optional<MockOutputs> render_mocks(const CollectorOptions &options, const std::vector<MockClassInfo> &mocks) {
    if (mocks.empty()) {
        return std::nullopt;
    }

    std::vector<const MockClassInfo *> classes;
    classes.reserve(mocks.size());
    for (const auto &cls : mocks) {
        classes.push_back(&cls);
    }
    std::ranges::sort(classes, {}, [](const MockClassInfo *cls) { return cls->qualified_name; });

    MockOutputs out;
    std::string header;
    header.reserve(classes.size() * 256);
    header += "// This file is auto-generated by gentest_codegen.\n// Do not edit manually.\n\n";
    header += "#pragma once\n\n";
    header += "#include <type_traits>\n\n";
    header += "// This header is included while inside namespace gentest\n\n";

    // Note: We require all mocked types to be complete before this registry is
    // included. The generated test TU ensures this by including project sources
    // first, then gentest/mock.h. For other TUs, users must include their
    // interfaces before including gentest/mock.h as well.
    for (const auto *cls : classes) {
        header += build_class_declaration(*cls);
    }
    header += "namespace detail {\n\n";
    for (const auto *cls : classes) {
        header += build_mock_access(*cls);
    }
    header += "} // namespace detail\n";

    out.registry_header     = std::move(header);
    out.implementation_unit = generate_implementation_header(classes);
    return out;
}

} // namespace gentest::codegen::render
