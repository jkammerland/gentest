#include "mock_manifest.hpp"

#include "render.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace gentest::codegen::mock_manifest {
namespace {
namespace json = llvm::json;

constexpr std::string_view kSchema = "gentest.mock_manifest.v1";

std::string to_string(TemplateParamKind kind) {
    switch (kind) {
    case TemplateParamKind::Type: return "type";
    case TemplateParamKind::Value: return "value";
    case TemplateParamKind::Template: return "template";
    }
    return "type";
}

std::string to_string(MockParamInfo::PassStyle style) {
    switch (style) {
    case MockParamInfo::PassStyle::Value: return "value";
    case MockParamInfo::PassStyle::LValueRef: return "lvalue_ref";
    case MockParamInfo::PassStyle::RValueRef: return "rvalue_ref";
    case MockParamInfo::PassStyle::ForwardingRef: return "forwarding_ref";
    }
    return "value";
}

std::string to_string(MockMethodCvQualifier qualifier) {
    switch (qualifier) {
    case MockMethodCvQualifier::None: return "none";
    case MockMethodCvQualifier::Const: return "const";
    case MockMethodCvQualifier::Volatile: return "volatile";
    case MockMethodCvQualifier::ConstVolatile: return "const_volatile";
    }
    return "none";
}

std::string to_string(MockMethodRefQualifier qualifier) {
    switch (qualifier) {
    case MockMethodRefQualifier::None: return "none";
    case MockMethodRefQualifier::LValue: return "lvalue";
    case MockMethodRefQualifier::RValue: return "rvalue";
    }
    return "none";
}

std::string to_string(MockClassInfo::DefinitionKind kind) {
    switch (kind) {
    case MockClassInfo::DefinitionKind::HeaderLike: return "header_like";
    case MockClassInfo::DefinitionKind::NamedModule: return "named_module";
    }
    return "header_like";
}

template <typename EnumT, typename ParseFn> bool parse_enum(std::string_view value, EnumT &out, ParseFn parse) {
    if (const auto parsed = parse(value); parsed.has_value()) {
        out = *parsed;
        return true;
    }
    return false;
}

std::optional<TemplateParamKind> parse_template_param_kind(std::string_view value) {
    if (value == "type") {
        return TemplateParamKind::Type;
    }
    if (value == "value") {
        return TemplateParamKind::Value;
    }
    if (value == "template") {
        return TemplateParamKind::Template;
    }
    return std::nullopt;
}

std::optional<MockParamInfo::PassStyle> parse_pass_style(std::string_view value) {
    if (value == "value") {
        return MockParamInfo::PassStyle::Value;
    }
    if (value == "lvalue_ref") {
        return MockParamInfo::PassStyle::LValueRef;
    }
    if (value == "rvalue_ref") {
        return MockParamInfo::PassStyle::RValueRef;
    }
    if (value == "forwarding_ref") {
        return MockParamInfo::PassStyle::ForwardingRef;
    }
    return std::nullopt;
}

std::optional<MockMethodCvQualifier> parse_cv_qualifier(std::string_view value) {
    if (value == "none") {
        return MockMethodCvQualifier::None;
    }
    if (value == "const") {
        return MockMethodCvQualifier::Const;
    }
    if (value == "volatile") {
        return MockMethodCvQualifier::Volatile;
    }
    if (value == "const_volatile") {
        return MockMethodCvQualifier::ConstVolatile;
    }
    return std::nullopt;
}

std::optional<MockMethodRefQualifier> parse_ref_qualifier(std::string_view value) {
    if (value == "none") {
        return MockMethodRefQualifier::None;
    }
    if (value == "lvalue") {
        return MockMethodRefQualifier::LValue;
    }
    if (value == "rvalue") {
        return MockMethodRefQualifier::RValue;
    }
    return std::nullopt;
}

std::optional<MockClassInfo::DefinitionKind> parse_definition_kind(std::string_view value) {
    if (value == "header_like") {
        return MockClassInfo::DefinitionKind::HeaderLike;
    }
    if (value == "named_module") {
        return MockClassInfo::DefinitionKind::NamedModule;
    }
    return std::nullopt;
}

json::Array string_array(const std::vector<std::string> &values) {
    json::Array out;
    for (const auto &value : values) {
        out.push_back(value);
    }
    return out;
}

json::Object template_param_object(const TemplateParamInfo &param) {
    return json::Object{
        {"kind", to_string(param.kind)},
        {"name", param.name},
        {"is_pack", param.is_pack},
        {"usage_spelling", param.usage_spelling},
    };
}

json::Array template_param_array(const std::vector<TemplateParamInfo> &params) {
    json::Array out;
    for (const auto &param : params) {
        out.push_back(template_param_object(param));
    }
    return out;
}

json::Object param_object(const MockParamInfo &param) {
    return json::Object{
        {"type", param.type},
        {"name", param.name},
        {"pass_style", to_string(param.pass_style)},
    };
}

json::Array param_array(const std::vector<MockParamInfo> &params) {
    json::Array out;
    for (const auto &param : params) {
        out.push_back(param_object(param));
    }
    return out;
}

json::Object ctor_object(const MockCtorInfo &ctor) {
    return json::Object{
        {"parameters", param_array(ctor.parameters)},
        {"template_prefix", ctor.template_prefix},
        {"template_params", template_param_array(ctor.template_params)},
        {"is_explicit", ctor.is_explicit},
        {"is_noexcept", ctor.is_noexcept},
    };
}

json::Array ctor_array(const std::vector<MockCtorInfo> &ctors) {
    json::Array out;
    for (const auto &ctor : ctors) {
        out.push_back(ctor_object(ctor));
    }
    return out;
}

json::Object qualifiers_object(const MockMethodQualifiers &qualifiers) {
    return json::Object{
        {"cv", to_string(qualifiers.cv)},
        {"ref", to_string(qualifiers.ref)},
        {"is_noexcept", qualifiers.is_noexcept},
    };
}

json::Object method_object(const MockMethodInfo &method) {
    return json::Object{
        {"qualified_name", method.qualified_name},
        {"method_name", method.method_name},
        {"return_type", method.return_type},
        {"parameters", param_array(method.parameters)},
        {"template_prefix", method.template_prefix},
        {"template_params", template_param_array(method.template_params)},
        {"is_static", method.is_static},
        {"is_virtual", method.is_virtual},
        {"is_pure_virtual", method.is_pure_virtual},
        {"qualifiers", qualifiers_object(method.qualifiers)},
    };
}

json::Array method_array(const std::vector<MockMethodInfo> &methods) {
    json::Array out;
    for (const auto &method : methods) {
        out.push_back(method_object(method));
    }
    return out;
}

json::Object namespace_scope_object(const MockNamespaceScopeInfo &scope) {
    return json::Object{
        {"name", scope.name},
        {"is_inline", scope.is_inline},
        {"is_exported", scope.is_exported},
        {"lexical_close_group", static_cast<std::int64_t>(scope.lexical_close_group)},
        {"reopen_prefix", scope.reopen_prefix},
    };
}

json::Array namespace_scope_array(const std::vector<MockNamespaceScopeInfo> &scopes) {
    json::Array out;
    for (const auto &scope : scopes) {
        out.push_back(namespace_scope_object(scope));
    }
    return out;
}

json::Object mock_object(const MockClassInfo &mock) {
    json::Object out{
        {"qualified_name", mock.qualified_name},
        {"display_name", mock.display_name},
        {"definition_file", mock.definition_file},
        {"definition_kind", to_string(mock.definition_kind)},
        {"use_files", string_array(mock.use_files)},
        {"definition_module_name", mock.definition_module_name},
        {"attachment_namespace_chain", namespace_scope_array(mock.attachment_namespace_chain)},
        {"derive_for_virtual", mock.derive_for_virtual},
        {"has_accessible_default_ctor", mock.has_accessible_default_ctor},
        {"has_virtual_destructor", mock.has_virtual_destructor},
        {"constructors", ctor_array(mock.constructors)},
        {"methods", method_array(mock.methods)},
    };
    if (mock.attachment_insertion_offset.has_value()) {
        out["attachment_insertion_offset"] = static_cast<std::int64_t>(*mock.attachment_insertion_offset);
    }
    return out;
}

bool require_string(const json::Object &obj, llvm::StringRef key, std::string &out, std::string &error) {
    const auto value = obj.getString(key);
    if (!value.has_value()) {
        error = "missing or invalid string field '" + key.str() + "'";
        return false;
    }
    out = value->str();
    return true;
}

bool optional_string(const json::Object &obj, llvm::StringRef key, std::string &out, std::string &error) {
    const auto *value = obj.get(key);
    if (value == nullptr) {
        return true;
    }
    const auto text = value->getAsString();
    if (!text.has_value()) {
        error = "invalid string field '" + key.str() + "'";
        return false;
    }
    out = text->str();
    return true;
}

bool optional_bool(const json::Object &obj, llvm::StringRef key, bool &out, std::string &error) {
    const auto *value = obj.get(key);
    if (value == nullptr) {
        return true;
    }
    const auto boolean = value->getAsBoolean();
    if (!boolean.has_value()) {
        error = "invalid boolean field '" + key.str() + "'";
        return false;
    }
    out = *boolean;
    return true;
}

bool string_vector(const json::Object &obj, llvm::StringRef key, std::vector<std::string> &out, std::string &error) {
    const auto *array = obj.getArray(key);
    if (array == nullptr) {
        return true;
    }
    out.clear();
    out.reserve(array->size());
    for (const auto &entry : *array) {
        const auto value = entry.getAsString();
        if (!value.has_value()) {
            error = "invalid string entry in '" + key.str() + "'";
            return false;
        }
        out.emplace_back(value->str());
    }
    return true;
}

bool parse_template_param(const json::Object &obj, TemplateParamInfo &out, std::string &error) {
    std::string kind;
    if (!require_string(obj, "kind", kind, error) || !parse_enum(kind, out.kind, parse_template_param_kind)) {
        if (error.empty()) {
            error = "invalid template parameter kind '" + kind + "'";
        }
        return false;
    }
    return optional_string(obj, "name", out.name, error) && optional_bool(obj, "is_pack", out.is_pack, error) &&
           optional_string(obj, "usage_spelling", out.usage_spelling, error);
}

bool parse_template_params(const json::Object &obj, llvm::StringRef key, std::vector<TemplateParamInfo> &out, std::string &error) {
    const auto *array = obj.getArray(key);
    if (array == nullptr) {
        return true;
    }
    out.clear();
    out.reserve(array->size());
    for (const auto &entry : *array) {
        const auto *param_obj = entry.getAsObject();
        if (param_obj == nullptr) {
            error = "invalid object entry in '" + key.str() + "'";
            return false;
        }
        TemplateParamInfo param;
        if (!parse_template_param(*param_obj, param, error)) {
            return false;
        }
        out.push_back(std::move(param));
    }
    return true;
}

bool parse_param(const json::Object &obj, MockParamInfo &out, std::string &error) {
    std::string pass_style;
    if (!require_string(obj, "type", out.type, error) || !optional_string(obj, "name", out.name, error) ||
        !require_string(obj, "pass_style", pass_style, error) || !parse_enum(pass_style, out.pass_style, parse_pass_style)) {
        if (error.empty()) {
            error = "invalid mock parameter pass_style '" + pass_style + "'";
        }
        return false;
    }
    return true;
}

bool parse_params(const json::Object &obj, llvm::StringRef key, std::vector<MockParamInfo> &out, std::string &error) {
    const auto *array = obj.getArray(key);
    if (array == nullptr) {
        return true;
    }
    out.clear();
    out.reserve(array->size());
    for (const auto &entry : *array) {
        const auto *param_obj = entry.getAsObject();
        if (param_obj == nullptr) {
            error = "invalid object entry in '" + key.str() + "'";
            return false;
        }
        MockParamInfo param;
        if (!parse_param(*param_obj, param, error)) {
            return false;
        }
        out.push_back(std::move(param));
    }
    return true;
}

bool parse_ctor(const json::Object &obj, MockCtorInfo &out, std::string &error) {
    return parse_params(obj, "parameters", out.parameters, error) && optional_string(obj, "template_prefix", out.template_prefix, error) &&
           parse_template_params(obj, "template_params", out.template_params, error) &&
           optional_bool(obj, "is_explicit", out.is_explicit, error) && optional_bool(obj, "is_noexcept", out.is_noexcept, error);
}

bool parse_ctors(const json::Object &obj, std::vector<MockCtorInfo> &out, std::string &error) {
    const auto *array = obj.getArray("constructors");
    if (array == nullptr) {
        return true;
    }
    out.clear();
    out.reserve(array->size());
    for (const auto &entry : *array) {
        const auto *ctor_obj = entry.getAsObject();
        if (ctor_obj == nullptr) {
            error = "invalid object entry in 'constructors'";
            return false;
        }
        MockCtorInfo ctor;
        if (!parse_ctor(*ctor_obj, ctor, error)) {
            return false;
        }
        out.push_back(std::move(ctor));
    }
    return true;
}

bool parse_qualifiers(const json::Object &obj, MockMethodQualifiers &out, std::string &error) {
    std::string cv;
    std::string ref;
    if (!require_string(obj, "cv", cv, error) || !parse_enum(cv, out.cv, parse_cv_qualifier) || !require_string(obj, "ref", ref, error) ||
        !parse_enum(ref, out.ref, parse_ref_qualifier) || !optional_bool(obj, "is_noexcept", out.is_noexcept, error)) {
        if (error.empty()) {
            error = "invalid mock method qualifiers";
        }
        return false;
    }
    return true;
}

bool parse_method(const json::Object &obj, MockMethodInfo &out, std::string &error) {
    if (!require_string(obj, "qualified_name", out.qualified_name, error) || !require_string(obj, "method_name", out.method_name, error) ||
        !require_string(obj, "return_type", out.return_type, error) || !parse_params(obj, "parameters", out.parameters, error) ||
        !optional_string(obj, "template_prefix", out.template_prefix, error) ||
        !parse_template_params(obj, "template_params", out.template_params, error) ||
        !optional_bool(obj, "is_static", out.is_static, error) || !optional_bool(obj, "is_virtual", out.is_virtual, error) ||
        !optional_bool(obj, "is_pure_virtual", out.is_pure_virtual, error)) {
        return false;
    }
    const auto *qualifiers = obj.getObject("qualifiers");
    if (qualifiers == nullptr) {
        error = "missing object field 'qualifiers'";
        return false;
    }
    return parse_qualifiers(*qualifiers, out.qualifiers, error);
}

bool parse_methods(const json::Object &obj, std::vector<MockMethodInfo> &out, std::string &error) {
    const auto *array = obj.getArray("methods");
    if (array == nullptr) {
        return true;
    }
    out.clear();
    out.reserve(array->size());
    for (const auto &entry : *array) {
        const auto *method_obj = entry.getAsObject();
        if (method_obj == nullptr) {
            error = "invalid object entry in 'methods'";
            return false;
        }
        MockMethodInfo method;
        if (!parse_method(*method_obj, method, error)) {
            return false;
        }
        out.push_back(std::move(method));
    }
    return true;
}

bool parse_namespace_scope(const json::Object &obj, MockNamespaceScopeInfo &out, std::string &error) {
    const auto lexical_close_group = obj.getInteger("lexical_close_group");
    if (lexical_close_group.has_value() && *lexical_close_group >= 0) {
        out.lexical_close_group = static_cast<std::size_t>(*lexical_close_group);
    } else if (obj.get("lexical_close_group") != nullptr) {
        error = "invalid integer field 'lexical_close_group'";
        return false;
    }
    return optional_string(obj, "name", out.name, error) && optional_bool(obj, "is_inline", out.is_inline, error) &&
           optional_bool(obj, "is_exported", out.is_exported, error) && optional_string(obj, "reopen_prefix", out.reopen_prefix, error);
}

bool parse_namespace_scopes(const json::Object &obj, std::vector<MockNamespaceScopeInfo> &out, std::string &error) {
    const auto *array = obj.getArray("attachment_namespace_chain");
    if (array == nullptr) {
        return true;
    }
    out.clear();
    out.reserve(array->size());
    for (const auto &entry : *array) {
        const auto *scope_obj = entry.getAsObject();
        if (scope_obj == nullptr) {
            error = "invalid object entry in 'attachment_namespace_chain'";
            return false;
        }
        MockNamespaceScopeInfo scope;
        if (!parse_namespace_scope(*scope_obj, scope, error)) {
            return false;
        }
        out.push_back(std::move(scope));
    }
    return true;
}

bool parse_mock(const json::Object &obj, MockClassInfo &out, std::string &error) {
    std::string definition_kind;
    if (!require_string(obj, "qualified_name", out.qualified_name, error) ||
        !optional_string(obj, "display_name", out.display_name, error) ||
        !require_string(obj, "definition_file", out.definition_file, error) ||
        !require_string(obj, "definition_kind", definition_kind, error) ||
        !parse_enum(definition_kind, out.definition_kind, parse_definition_kind) ||
        !string_vector(obj, "use_files", out.use_files, error) ||
        !optional_string(obj, "definition_module_name", out.definition_module_name, error) ||
        !parse_namespace_scopes(obj, out.attachment_namespace_chain, error) ||
        !optional_bool(obj, "derive_for_virtual", out.derive_for_virtual, error) ||
        !optional_bool(obj, "has_accessible_default_ctor", out.has_accessible_default_ctor, error) ||
        !optional_bool(obj, "has_virtual_destructor", out.has_virtual_destructor, error) || !parse_ctors(obj, out.constructors, error) ||
        !parse_methods(obj, out.methods, error)) {
        if (error.empty()) {
            error = "invalid mock definition_kind '" + definition_kind + "'";
        }
        return false;
    }

    if (out.display_name.empty()) {
        out.display_name = out.qualified_name;
    }
    if (const auto offset = obj.getInteger("attachment_insertion_offset"); offset.has_value()) {
        if (*offset < 0) {
            error = "invalid integer field 'attachment_insertion_offset'";
            return false;
        }
        out.attachment_insertion_offset = static_cast<std::size_t>(*offset);
    }
    return true;
}

bool ensure_parent_dir(const std::filesystem::path &path, std::string &error) {
    if (!path.has_parent_path()) {
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "failed to create directory '" + path.parent_path().string() + "': " + ec.message();
        return false;
    }
    return true;
}

} // namespace

std::string serialize(std::vector<MockClassInfo> mocks, std::vector<std::string> mock_output_domain_modules) {
    std::ranges::sort(mocks, {}, [](const MockClassInfo &mock) {
        return std::tie(mock.qualified_name, mock.definition_file, mock.definition_module_name, mock.definition_kind);
    });

    json::Array domain_modules;
    for (const auto &module_name : mock_output_domain_modules) {
        domain_modules.push_back(module_name);
    }

    json::Array mock_values;
    for (const auto &mock : mocks) {
        mock_values.push_back(mock_object(mock));
    }

    json::Object root{
        {"schema", std::string(kSchema)},
        {"mock_output_domain_modules", std::move(domain_modules)},
        {"mocks", std::move(mock_values)},
    };

    std::string              text;
    llvm::raw_string_ostream os(text);
    os << json::Value(std::move(root));
    os << '\n';
    os.flush();
    return text;
}

bool write(const std::filesystem::path &path, const std::vector<MockClassInfo> &mocks, std::vector<std::string> mock_output_domain_modules,
           std::string &error) {
    if (!ensure_parent_dir(path, error)) {
        return false;
    }
    const std::string content = serialize(mocks, std::move(mock_output_domain_modules));
    {
        std::ifstream existing(path, std::ios::binary);
        if (existing) {
            const std::string existing_content{std::istreambuf_iterator<char>(existing), std::istreambuf_iterator<char>()};
            if (existing_content == content) {
                return true;
            }
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "failed to open mock manifest '" + path.string() + "'";
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    if (!out) {
        error = "failed to write mock manifest '" + path.string() + "'";
        return false;
    }
    return true;
}

ReadResult read(const std::filesystem::path &path) {
    ReadResult result;
    auto       buffer = llvm::MemoryBuffer::getFile(path.string());
    if (!buffer) {
        result.error = "failed to read mock manifest '" + path.string() + "': " + buffer.getError().message();
        return result;
    }

    auto parsed = json::parse((*buffer)->getBuffer());
    if (!parsed) {
        result.error = "failed to parse mock manifest '" + path.string() + "': " + llvm::toString(parsed.takeError());
        return result;
    }

    const auto *root = parsed->getAsObject();
    if (root == nullptr) {
        result.error = "mock manifest root must be an object";
        return result;
    }

    const auto schema = root->getString("schema");
    if (!schema.has_value() || *schema != llvm::StringRef{kSchema.data(), kSchema.size()}) {
        result.error = "unsupported mock manifest schema";
        return result;
    }

    if (!string_vector(*root, "mock_output_domain_modules", result.mock_output_domain_modules, result.error)) {
        return result;
    }

    const auto *mocks = root->getArray("mocks");
    if (mocks == nullptr) {
        result.error = "mock manifest is missing 'mocks'";
        return result;
    }

    result.mocks.reserve(mocks->size());
    for (const auto &entry : *mocks) {
        const auto *mock_obj = entry.getAsObject();
        if (mock_obj == nullptr) {
            result.error = "invalid object entry in 'mocks'";
            result.mocks.clear();
            return result;
        }
        MockClassInfo mock;
        if (!parse_mock(*mock_obj, mock, result.error)) {
            result.mocks.clear();
            return result;
        }
        result.mocks.push_back(std::move(mock));
    }
    return result;
}

} // namespace gentest::codegen::mock_manifest
