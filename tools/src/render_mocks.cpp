#include "render_mocks.hpp"
#include "templates_mocks.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fmt/format.h>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {
class RenderBuffer {
public:
    void reserve(std::size_t size) { buffer_.reserve(size); }

    void append_raw(std::string_view text) { buffer_.append(text.data(), text.data() + text.size()); }

    template <typename... Args>
    void append(fmt::format_string<Args...> format_string, Args &&...args) {
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
    RenderBuffer block;
    block.append("{0}auto token = this->__gentest_state_.identify(&{1}::{2}{3});\n", indent, fq_type, method.method_name,
                 tpl_usage);
    const std::string return_type   = ensure_global_qualifiers(method.return_type);
    const std::string args          = argument_list(method);
    const bool        returns_value = method.return_type != "void";
    const std::string fq_method     = fmt::format("::{}", method.qualified_name);
    const std::string dispatch_args = args.empty() ? std::string{} : fmt::format(", {}", args);
    block.append("{0}{1}this->__gentest_state_.template dispatch<{2}>(token, \"{3}\"{4});\n", indent,
                 returns_value ? "return " : "", return_type, fq_method, dispatch_args);
    return block.str();
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
    RenderBuffer out;
    out.append_raw(open_namespaces(ns));
    out.append("struct {} {{\n", type_name);
    if (cls.has_virtual_destructor) {
        out.append("    virtual ~{}() {{}}\n", type_name);
    }
    for (const auto &m : cls.methods) {
        if (!m.template_prefix.empty()) {
            out.append("    {}\n", m.template_prefix);
        }
        const char *virt = m.is_virtual ? "virtual " : "";
        out.append("    {0}{1} {2}({3}){4};\n", virt, ensure_global_qualifiers(m.return_type), m.method_name,
                   join_parameter_list(m.parameters), qualifiers_for(m));
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
        case MockParamInfo::PassStyle::ForwardingRef:
            return ForwardingMode::Forward;
        case MockParamInfo::PassStyle::LValueRef:
            return ForwardingMode::Borrow;
        case MockParamInfo::PassStyle::RValueRef:
            return ForwardingMode::Move;
        case MockParamInfo::PassStyle::Value:
            return value_pass_policy == ValuePassPolicy::Copy ? ForwardingMode::Copy : ForwardingMode::Move;
        }
        return ForwardingMode::Move;
    }

    [[nodiscard]] std::string expr_for(const MockParamInfo &param) const {
        switch (mode_for(param)) {
        case ForwardingMode::Forward: {
            std::string out;
            out.reserve(param.name.size() * 2 + 26);
            out += "std::forward<decltype(";
            out += param.name;
            out += ")>(";
            out += param.name;
            out += ')';
            return out;
        }
        case ForwardingMode::Borrow:
        case ForwardingMode::Copy:
            return param.name;
        case ForwardingMode::Move:
            break;
        }
        std::string out;
        out.reserve(param.name.size() + 10);
        out += "std::move(";
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
    if (!method.template_prefix.empty()) {
        decl.append("{}\n", method.template_prefix);
    }
    decl.append("{} {}({}){}", method.return_type, method.method_name, join_parameter_list(method.parameters),
                qualifiers_for(method));
    if (cls.derive_for_virtual && method.is_virtual) {
        decl.append_raw(" override");
    }
    if (!method.template_prefix.empty()) {
        // Inline definition for template methods (must be visible to callers)
        const std::string fq_type = fmt::format("::{}", cls.qualified_name);
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
        decl.append_raw(" {\n");
        decl.append_raw(dispatch_block("        ", method, fq_type, tpl_use));
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
    RenderBuffer header;
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

std::string build_mock_access(const MockClassInfo &cls) {
    RenderBuffer body;
    const std::string fq_type = fmt::format("::{}", cls.qualified_name);
    body.append("template <>\nstruct MockAccess<mock<{}>> {{\n", fq_type);
    body.append_raw("    template <class MethodPtr>\n");
    body.append("    static auto expect(mock<{0}> &instance, MethodPtr method) {{\n", fq_type);
    body.append_raw("        using ::gentest::detail::mocking::ExpectationHandle;\n");
    body.append_raw("        using ::gentest::detail::mocking::MethodTraits;\n");
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
        const std::string fq_method    = fmt::format("::{}", method.qualified_name);
        const std::string branch_intro = first_branch ? "        if constexpr" : "        else if constexpr";
        first_branch                   = false;
        body.append("{0} (std::is_same_v<MethodPtr, {1}>) {{\n", branch_intro, pointer_type);
        body.append("            if (method == static_cast<MethodPtr>(&{0}::{1})) {{\n", fq_type, method.method_name);
        body.append("                auto token = instance.__gentest_state_.identify(&{0}::{1});\n"
                    "                auto expectation = instance.__gentest_state_.template push_expectation<{2}>(token, \"{3}\");\n"
                    "                return ExpectationHandle<{4}>{{expectation, \"{3}\"}};\n",
                    fq_type, method.method_name, push_args, fq_method, signature);
        body.append_raw("            }\n");
        body.append_raw("        }\n");
    }
    body.append_raw("        using Signature = typename MethodTraits<MethodPtr>::Signature;\n");
    body.append_raw("        auto token = instance.__gentest_state_.identify(method);\n");
    body.append_raw(
        "        auto expectation = ::gentest::detail::mocking::ExpectationPusher<Signature>::push(instance.__gentest_state_, token, \"(mock method)\");\n");
    body.append_raw("        return ExpectationHandle<Signature>{expectation, \"(mock method)\"};\n");
    body.append_raw("    }\n");
    body.append_raw("\n");
    body.append("    static void set_nice(mock<{0}> &instance, bool v) {{ instance.__gentest_state_.set_nice(v); }}\n", fq_type);
    body.append_raw("};\n\n");
    return body.str();
}

std::string method_definition(const MockClassInfo &cls, const MockMethodInfo &method) {
    RenderBuffer def;
    const std::string fq_type = fmt::format("::{}", cls.qualified_name);
    if (!method.template_prefix.empty()) {
        def.append("{}\n", method.template_prefix);
    }
    def.append("{} gentest::mock<{}>::{}({}){} {{\n", ensure_global_qualifiers(method.return_type), fq_type,
               method.method_name, join_parameter_list(method.parameters), qualifiers_for(method));
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
    def.append_raw(dispatch_block("    ", method, fq_type, tpl_usage));
    def.append_raw("}\n");
    return def.str();
}

struct DefinitionIncludeBlock {
    std::string text;
    std::string error;
};

DefinitionIncludeBlock build_definition_include_block(const CollectorOptions &options, const std::vector<const MockClassInfo *> &classes) {
    namespace fs = std::filesystem;
    DefinitionIncludeBlock result;
    std::set<std::string>  includes;

    fs::path registry_dir = options.mock_registry_path.parent_path();
    if (registry_dir.empty()) {
        registry_dir = ".";
    }
    std::error_code ec;
    if (registry_dir.is_relative()) {
        const fs::path abs_registry_dir = fs::absolute(registry_dir, ec);
        if (!ec) {
            registry_dir = abs_registry_dir;
        }
        ec.clear();
    }
    registry_dir = registry_dir.lexically_normal();

    for (const auto *cls : classes) {
        if (!cls) {
            continue;
        }
        if (cls->definition_file.empty()) {
            result.error = fmt::format("mock renderer: missing definition file for '{}'", cls->qualified_name);
            return result;
        }

        fs::path def_path{cls->definition_file};
        if (def_path.is_relative()) {
            const fs::path abs_def_path = fs::absolute(def_path, ec);
            if (ec) {
                result.error = fmt::format("mock renderer: failed to resolve definition path '{}' for '{}': {}", def_path.string(),
                                           cls->qualified_name, ec.message());
                return result;
            }
            def_path = abs_def_path;
        }
        def_path = def_path.lexically_normal();

        fs::path rel_path = fs::proximate(def_path, registry_dir, ec);
        if (ec || rel_path.empty() || rel_path.is_absolute()) {
            result.error = fmt::format("mock renderer: could not compute source-relative include from '{}' to '{}' for '{}'",
                                       registry_dir.generic_string(), def_path.generic_string(), cls->qualified_name);
            return result;
        }
        includes.insert(rel_path.generic_string());
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

std::string generate_implementation_header(const std::vector<const MockClassInfo *> &mocks) {
    RenderBuffer impl;
    impl.reserve(mocks.size() * 256);
    impl.append_raw(tpl::mocks::impl_preamble);
    // Note: Include order matters. This header is intended to be included
    // after the test sources (so original types are complete) and after
    // including gentest/mock.h in the including TU.

    for (const auto *cls : mocks) {
        const std::string fq_type = fmt::format("::{}", cls->qualified_name);
        if (cls->has_accessible_default_ctor) {
            impl.append("inline mock<{0}>::mock() = default;\n", cls->qualified_name);
        }
        for (const auto &ctor : cls->constructors) {
            if (!ctor.template_prefix.empty()) {
                impl.append("{}\n", ctor.template_prefix);
            }
            impl.append("inline mock<{0}>::mock(", fq_type);
            impl.append("{}", join_parameter_list(ctor.parameters));
            impl.append_raw(")");
            if (ctor.is_noexcept) {
                impl.append_raw(" noexcept");
            }
            if (cls->derive_for_virtual) {
                impl.append_raw(" : ");
                impl.append("{}", fq_type);
                impl.append_raw("(");
                for (std::size_t i = 0; i < ctor.parameters.size(); ++i) {
                    if (i != 0)
                        impl.append_raw(", ");
                    const auto &p = ctor.parameters[i];
                    impl.append("{}", argument_expr(p));
                }
                impl.append_raw(")");
                impl.append_raw(" {}\n");
            } else {
                impl.append_raw(" {\n");
                for (const auto &p : ctor.parameters) {
                    impl.append("    (void){};\n", p.name);
                }
                impl.append_raw("}\n");
            }
            impl.append_raw("\n");
        }
        impl.append("inline mock<{0}>::~mock() {{ this->__gentest_state_.verify_all(); }}\n\n", cls->qualified_name);
        for (const auto &method : cls->methods) {
            if (!method.template_prefix.empty()) continue; // defined inline in class declaration
            std::string def = method_definition(*cls, method);
            // Prefix with inline to be ODR-safe if included in multiple TUs
            def.insert(0, "inline ");
            impl.append_raw(def);
            impl.append_raw("\n");
        }
        impl.append_raw("\n");
    }

    impl.append_raw(tpl::mocks::impl_footer);
    return impl.str();
}

} // namespace

MockRenderResult render_mocks(const CollectorOptions &options, const std::vector<MockClassInfo> &mocks) {
    MockRenderResult result;
    if (mocks.empty()) {
        return result;
    }

    std::vector<const MockClassInfo *> classes;
    classes.reserve(mocks.size());
    for (const auto &cls : mocks) {
        classes.push_back(&cls);
    }
    std::ranges::sort(classes, {}, [](const MockClassInfo *cls) { return cls->qualified_name; });

    MockOutputs out;
    RenderBuffer header;
    header.reserve(classes.size() * 256);
    header.append_raw(tpl::mocks::registry_preamble);

    const DefinitionIncludeBlock include_block = build_definition_include_block(options, classes);
    if (!include_block.error.empty()) {
        result.error = include_block.error;
        return result;
    }
    header.append_raw(include_block.text);
    header.append_raw("namespace gentest {\n\n");

    for (const auto *cls : classes) {
        header.append_raw(build_class_declaration(*cls));
    }
    header.append_raw("namespace detail {\n\n");
    for (const auto *cls : classes) {
        header.append_raw(build_mock_access(*cls));
    }
    header.append_raw("} // namespace detail\n");
    header.append_raw("} // namespace gentest\n");

    out.registry_header     = header.str();
    out.implementation_unit = generate_implementation_header(classes);
    result.outputs          = std::move(out);
    return result;
}

} // namespace gentest::codegen::render
