#include "render_mocks.hpp"

#include <algorithm>
#include <filesystem>
#include <cctype>
#include <fmt/core.h>
#include <sstream>
#include <string>
#include <utility>

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

// Forward declarations
std::string argument_list(const MockMethodInfo &method);

std::string join_parameter_list(const std::vector<MockParamInfo> &params) {
    std::string out;
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i != 0)
            out += ", ";
        out += params[i].type;
        if (!params[i].name.empty()) {
            out += ' ';
            out += params[i].name;
        }
    }
    return out;
}

std::string join_type_list(const std::vector<MockParamInfo> &params) {
    std::string out;
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i != 0)
            out += ", ";
        out += params[i].type;
    }
    return out;
}

std::string ensure_global_qualifiers(std::string value) {
    std::size_t pos = value.find("::");
    if (pos != std::string::npos) {
        std::size_t insert_pos = pos;
        while (insert_pos > 0 && (std::isalnum(static_cast<unsigned char>(value[insert_pos - 1])) || value[insert_pos - 1] == '_')) {
            --insert_pos;
        }
        if (!(insert_pos >= 2 && value[insert_pos - 1] == ':' && value[insert_pos - 2] == ':')) {
            value.insert(insert_pos, "::");
        }
    }
    return value;
}

std::string signature_from(const MockMethodInfo &method) {
    std::string sig = ensure_global_qualifiers(method.return_type);
    sig += '(';
    sig += ensure_global_qualifiers(join_type_list(method.parameters));
    sig += ')';
    return sig;
}

std::string pointer_type_for(const MockClassInfo &cls, const MockMethodInfo &method) {
    std::string ptr;
    ptr += ensure_global_qualifiers(method.return_type);
    ptr += " (";
    ptr += "::";
    ptr += cls.qualified_name;
    ptr += "::*)(";
    ptr += ensure_global_qualifiers(join_type_list(method.parameters));
    ptr += ')';
    if (method.is_const)
        ptr += " const";
    if (method.is_volatile)
        ptr += " volatile";
    if (!method.ref_qualifier.empty()) {
        ptr += ' ';
        ptr += method.ref_qualifier;
    }
    if (method.is_noexcept)
        ptr += " noexcept";
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
    std::size_t count = 1;
    for (std::size_t pos = 0; pos != std::string::npos; ) {
        pos = ns.find("::", pos == 0 ? 0 : pos + 2);
        ++count;
    }
    // count is approximate; simpler: count number of components
    count = 0;
    for (std::size_t i = 0, j; i < ns.size(); i = j + 2) {
        j = ns.find("::", i);
        if (j == std::string::npos) { ++count; break; }
        ++count;
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
    // If the parameter is an rvalue reference or passed by value, forward as std::move
    if (param.type.find("&&") != std::string::npos) {
        return fmt::format("std::move({})", param.name);
    }
    if (param.type.find('&') == std::string::npos) {
        return fmt::format("std::move({})", param.name);
    }
    return param.name;
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
        const std::string fq_type = fmt::format("::{}", cls.qualified_name);
        const std::string args    = argument_list(method);
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
        decl += fmt::format("        auto token = this->__gentest_state_.identify(&{}::{}{});\n", fq_type, method.method_name, tpl_use);
        const bool returns_value = method.return_type != "void";
        const std::string fq_method = fmt::format("::{}", method.qualified_name);
        decl += fmt::format("        {}this->__gentest_state_.template dispatch<{}>(token, \"{}\"{});\n",
                            returns_value ? "return " : "", ensure_global_qualifiers(method.return_type), fq_method,
                            args.empty() ? std::string{} : ", " + args);
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
    block += "    mock();\n";
    block += fmt::format("    ~mock(){};\n", cls.has_virtual_destructor && cls.derive_for_virtual ? " override" : "");
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
    const std::string fq_type = fmt::format("::{}", cls.qualified_name);
    header += fmt::format("template <>\nstruct mock<{}>{} {{\n", fq_type,
                          cls.derive_for_virtual ? fmt::format(" final : public {}", fq_type) : std::string{});
    header += fmt::format("    using __gentest_target = {};\n", fq_type);
    header += constructors_block(cls);
    if (!cls.methods.empty()) {
        header += method_declarations_block(cls);
    }
    header += "\n";
    header += "  private:\n";
    header += fmt::format("    friend struct detail::MockAccess<mock<{}>>;\n", fq_type);
    header += "    mutable detail::mocking::InstanceState __gentest_state_;\n";
    header += "};\n\n";
    return header;
}

std::string build_mock_access(const MockClassInfo &cls) {
    std::string body;
    const std::string fq_type = fmt::format("::{}", cls.qualified_name);
    body += fmt::format("template <>\nstruct MockAccess<mock<{}>> {{\n", fq_type);
    body += "    template <class MethodPtr>\n";
    body += fmt::format("    static auto expect(mock<{0}> &instance, MethodPtr method) {{\n", fq_type);
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
        const std::string fq_method = fmt::format("::{}", method.qualified_name);
        const std::string branch_intro = first_branch ? "        if constexpr" : "        else if constexpr";
        first_branch                   = false;
        body += fmt::format("{0} (std::is_same_v<MethodPtr, {1}>) {{\n", branch_intro, pointer_type);
        body += fmt::format("            if (method == static_cast<MethodPtr>(&{0}::{1})) {{\n", fq_type, method.method_name);
        body += fmt::format("                auto token = instance.__gentest_state_.identify(&{0}::{1});\n"
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
    body += fmt::format("    static void set_nice(mock<{0}> &instance, bool v) {{ instance.__gentest_state_.set_nice(v); }}\n", fq_type);
    body += "};\n\n";
    return body;
}

std::string method_definition(const MockClassInfo &cls, const MockMethodInfo &method) {
    std::string def;
    const std::string fq_type = fmt::format("::{}", cls.qualified_name);
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
    def += ensure_global_qualifiers(join_parameter_list(method.parameters));
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
    def += fmt::format("    auto token = this->__gentest_state_.identify(&{}::{}{});\n", fq_type, method.method_name, tpl_usage);
    const std::string return_type   = ensure_global_qualifiers(method.return_type);
    const std::string args          = argument_list(method);
    const bool        returns_value = method.return_type != "void";
    const std::string fq_method     = fmt::format("::{}", method.qualified_name);
    def += fmt::format("    {}this->__gentest_state_.template dispatch<{}>(token, \"{}\"{});\n",
                       returns_value ? "return " : "", return_type, fq_method,
                       args.empty() ? std::string{} : ", " + args);
    def += "}\n";
    return def;
}

std::string include_sources_block(const CollectorOptions &options, const std::filesystem::path &base_dir) {
    namespace fs = std::filesystem;
    std::string includes;
    for (const auto &src : options.sources) {
        fs::path        spath(src);
        std::error_code ec;
        fs::path        rel = fs::proximate(spath, base_dir, ec);
        if (ec)
            rel = spath;
        includes += fmt::format("#include \"{}\"\n", rel.generic_string());
    }
    return includes;
}

std::string generate_implementation_header(const std::vector<MockClassInfo> &mocks) {
    std::string impl;
    impl += "// This file is auto-generated by gentest_codegen.\n// Do not edit manually.\n\n";
    impl += "#pragma once\n\n";
    impl += "#include <utility>\n\n";
    // Note: Include order matters. This header is intended to be included
    // after the test sources (so original types are complete) and after
    // including gentest/mock.h in the including TU.
    impl += "namespace gentest {\n\n";

    for (const auto &cls : mocks) {
        impl += fmt::format("inline mock<{0}>::mock() = default;\n", cls.qualified_name);
        impl += fmt::format("inline mock<{0}>::~mock() {{ this->__gentest_state_.verify_all(); }}\n\n", cls.qualified_name);
        for (const auto &method : cls.methods) {
            if (!method.template_prefix.empty()) continue; // defined inline in class declaration
            std::string def = method_definition(cls, method);
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

    std::vector<MockClassInfo> classes = mocks;
    std::sort(classes.begin(), classes.end(),
              [](const MockClassInfo &lhs, const MockClassInfo &rhs) { return lhs.qualified_name < rhs.qualified_name; });

    MockOutputs out;
    std::string header;
    header += "// This file is auto-generated by gentest_codegen.\n// Do not edit manually.\n\n";
    header += "#pragma once\n\n";
    header += "#include <type_traits>\n\n";
    header += "// This header is included while inside namespace gentest\n\n";

    // Note: We require all mocked types to be complete before this registry is
    // included. The generated test TU ensures this by including project sources
    // first, then gentest/mock.h. For other TUs, users must include their
    // interfaces before including gentest/mock.h as well.
    for (const auto &cls : classes) {
        header += build_class_declaration(cls);
    }
    header += "namespace detail {\n\n";
    for (const auto &cls : classes) {
        header += build_mock_access(cls);
    }
    header += "} // namespace detail\n";

    out.registry_header     = std::move(header);
    out.implementation_unit = generate_implementation_header(classes);
    return out;
}

} // namespace gentest::codegen::render
