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

std::string argument_expr(const MockParamInfo &param) {
    if (param.type.find("&&") != std::string::npos) {
        return fmt::format("std::move({})", param.name);
    }
    return param.name;
}

std::string build_method_declaration(const MockClassInfo &cls, const MockMethodInfo &method) {
    std::string decl = method.return_type;
    decl += ' ';
    decl += method.method_name;
    decl += '(';
    decl += join_parameter_list(method.parameters);
    decl += ')';
    decl += qualifiers_for(method);
    if (cls.derive_for_virtual && method.is_virtual) {
        decl += " override";
    }
    decl += ';';
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
    body += "        ::gentest::detail::record_failure(\"gentest::expect received unsupported method pointer for mock<" + cls.qualified_name + ">\");\n";
    body += "        using Signature = typename MethodTraits<MethodPtr>::Signature;\n";
    body += "        return ExpectationHandle<Signature>{};\n";
    body += "    }\n";
    body += "};\n\n";
    return body;
}

std::string method_definition(const MockClassInfo &cls, const MockMethodInfo &method) {
    std::string def;
    const std::string fq_type = fmt::format("::{}", cls.qualified_name);
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
    def += fmt::format("    auto token = this->__gentest_state_.identify(&{}::{});\n", fq_type, method.method_name);
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

std::string generate_implementation(const CollectorOptions &options, const std::vector<MockClassInfo> &mocks) {
    namespace fs = std::filesystem;
    std::string impl;
    impl += "// This file is auto-generated by gentest_codegen.\n// Do not edit manually.\n\n";
    impl += "#include <utility>\n\n";

    const fs::path base_dir = options.mock_impl_path.has_parent_path() ? options.mock_impl_path.parent_path() : fs::current_path();
    impl += "#define GENTEST_BUILDING_MOCKS\n";
    impl += include_sources_block(options, base_dir);
    impl += "#undef GENTEST_BUILDING_MOCKS\n\n";

    impl += "#include \"gentest/mock.h\"\n\n";

    impl += "namespace gentest {\n\n";

    for (const auto &cls : mocks) {
        impl += fmt::format("mock<{0}>::mock() = default;\n", cls.qualified_name);
        impl += fmt::format("mock<{0}>::~mock() {{ this->__gentest_state_.verify_all(); }}\n\n", cls.qualified_name);
        for (const auto &method : cls.methods) {
            impl += method_definition(cls, method);
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
    for (const auto &cls : classes) {
        header += build_class_declaration(cls);
    }
    header += "namespace detail {\n\n";
    for (const auto &cls : classes) {
        header += build_mock_access(cls);
    }
    header += "} // namespace detail\n";

    out.registry_header     = std::move(header);
    out.implementation_unit = generate_implementation(options, classes);
    return out;
}

} // namespace gentest::codegen::render
