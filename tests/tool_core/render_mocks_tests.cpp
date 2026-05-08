#include "render_mocks.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using gentest::codegen::CollectorOptions;
using gentest::codegen::MockBackend;
using gentest::codegen::MockClassInfo;
using gentest::codegen::MockMethodCvQualifier;
using gentest::codegen::MockMethodInfo;
using gentest::codegen::MockMethodRefQualifier;
using gentest::codegen::MockParamInfo;
using gentest::codegen::render::MockGeneratedFile;
using gentest::codegen::render::MockRenderResult;

namespace {

struct Run {
    int failures = 0;

    void expect(bool ok, std::string_view msg) {
        if (!ok) {
            ++failures;
            std::cerr << "FAIL: " << msg << "\n";
        }
    }

    void contains(std::string_view haystack, std::string_view needle, std::string_view msg) {
        expect(haystack.find(needle) != std::string_view::npos, msg);
    }

    void excludes(std::string_view haystack, std::string_view needle, std::string_view msg) {
        expect(haystack.find(needle) == std::string_view::npos, msg);
    }
};

CollectorOptions mock_options(MockBackend backend = MockBackend::Gentest) {
    CollectorOptions options;
    options.mock_backend                 = backend;
    options.mock_registry_path           = "generated/public_mocks.hpp";
    options.mock_impl_path               = "generated/public_mocks_impl.hpp";
    options.mock_domain_registry_outputs = {"generated/public_mocks_registry.hpp"};
    options.mock_domain_impl_outputs     = {"generated/public_mocks_inline.hpp"};
    return options;
}

MockMethodInfo compute_method() {
    MockMethodInfo method;
    method.qualified_name           = "fixture::Service::compute";
    method.method_name              = "compute";
    method.return_type              = "std::pair<int, int>";
    method.is_virtual               = true;
    method.is_pure_virtual          = true;
    method.qualifiers.cv            = MockMethodCvQualifier::Const;
    method.qualifiers.ref           = MockMethodRefQualifier::LValue;
    method.qualifiers.is_noexcept   = true;
    method.parameters               = {MockParamInfo{.type = "std::vector<int>", .name = "values"}};
    method.parameters[0].pass_style = MockParamInfo::PassStyle::LValueRef;
    return method;
}

MockClassInfo service_mock() {
    MockClassInfo cls;
    cls.qualified_name              = "fixture::Service";
    cls.display_name                = "fixture::Service";
    cls.definition_file             = "fixture/service.hpp";
    cls.derive_for_virtual          = true;
    cls.has_accessible_default_ctor = true;
    cls.has_virtual_destructor      = true;
    cls.methods                     = {compute_method()};
    return cls;
}

const MockGeneratedFile *find_file(const MockRenderResult &result, std::string_view filename) {
    if (!result.outputs.has_value()) {
        return nullptr;
    }
    for (const auto &file : result.outputs->additional_files) {
        if (file.path.filename().generic_string() == filename) {
            return &file;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    Run t;

    {
        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(), {service_mock()});
        t.expect(result.error.empty(), "gentest backend renders without an error");
        const MockGeneratedFile *registry = find_file(result, "public_mocks_registry.hpp");
        t.expect(registry != nullptr, "gentest backend emits a registry file");
        if (registry != nullptr) {
            t.contains(registry->content, "#include \"../fixture/service.hpp\"", "gentest backend includes target definition");
            t.contains(registry->content, "detail::MockAccess", "gentest backend keeps native expectation access");
            t.contains(registry->content, "__gentest_state_", "gentest backend keeps native mock state");
            t.excludes(registry->content, "#include <gmock/gmock.h>", "gentest backend does not include gmock");
            t.excludes(registry->content, "#include <trompeloeil/mock.hpp>", "gentest backend does not include trompeloeil");
        }
    }

    {
        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(MockBackend::GMock), {service_mock()});
        t.expect(result.error.empty(), "gmock backend renders without an error");
        const MockGeneratedFile *registry = find_file(result, "public_mocks_registry.hpp");
        t.expect(registry != nullptr, "gmock backend emits a registry file");
        if (registry != nullptr) {
            t.contains(registry->content, "#include <gmock/gmock.h>", "gmock backend includes gmock");
            t.contains(registry->content, "namespace fixture {\nnamespace mocks {\n", "gmock backend mirrors the target namespace");
            t.contains(registry->content, "struct ServiceMock final : public ::fixture::Service",
                       "gmock backend emits a native mock class");
            t.contains(registry->content, "using __gentest_mock_0_return = ::std::pair<int, int>;",
                       "gmock backend aliases comma-bearing return types");
            t.contains(registry->content, "using __gentest_mock_0_arg_0 = ::std::vector<int>;",
                       "gmock backend aliases comma-bearing argument types");
            t.contains(registry->content,
                       "MOCK_METHOD(__gentest_mock_0_return, compute, (__gentest_mock_0_arg_0), (const, noexcept, ref(&), override));",
                       "gmock backend preserves cv/ref/noexcept/override qualifiers");
            t.excludes(registry->content, "#include \"gentest/mock_fwd.h\"", "gmock backend does not include gentest mock forwarding");
            t.excludes(registry->content, "namespace gentest", "gmock backend does not emit into gentest namespace");
            t.excludes(registry->content, "struct mock<", "gmock backend does not specialize gentest::mock");
            t.excludes(registry->content, "detail::MockAccess", "gmock backend does not emit native gentest access plumbing");
        }
    }

    {
        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(MockBackend::Trompeloeil), {service_mock()});
        t.expect(result.error.empty(), "trompeloeil backend renders without an error");
        const MockGeneratedFile *registry = find_file(result, "public_mocks_registry.hpp");
        t.expect(registry != nullptr, "trompeloeil backend emits a registry file");
        if (registry != nullptr) {
            t.contains(registry->content, "#include <trompeloeil/mock.hpp>", "trompeloeil backend includes trompeloeil");
            t.contains(registry->content, "namespace fixture {\nnamespace mocks {\n", "trompeloeil backend mirrors the target namespace");
            t.contains(registry->content, "struct ServiceMock final : public ::fixture::Service",
                       "trompeloeil backend emits a native mock class");
            t.contains(registry->content,
                       "MAKE_CONST_MOCK(compute, auto (__gentest_mock_0_arg_0) & -> __gentest_mock_0_return, override, noexcept);",
                       "trompeloeil backend preserves const/ref/noexcept/override qualifiers");
            t.excludes(registry->content, "#include \"gentest/mock_fwd.h\"",
                       "trompeloeil backend does not include gentest mock forwarding");
            t.excludes(registry->content, "namespace gentest", "trompeloeil backend does not emit into gentest namespace");
            t.excludes(registry->content, "struct mock<", "trompeloeil backend does not specialize gentest::mock");
            t.excludes(registry->content, "__gentest_state_", "trompeloeil backend does not emit native gentest state");
        }
    }

    {
        MockClassInfo cls              = service_mock();
        cls.methods[0].template_prefix = "template <typename T>";
        cls.methods[0].template_params.push_back({.name = "T"});
        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(MockBackend::GMock), {std::move(cls)});
        t.expect(!result.error.empty(), "gmock backend rejects member templates");
        t.contains(result.error, "does not support member function templates", "gmock backend diagnoses member templates");
    }

    {
        MockClassInfo cls             = service_mock();
        cls.qualified_name            = "fixture::Repo<int>";
        cls.display_name              = cls.qualified_name;
        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(MockBackend::GMock), {std::move(cls)});
        t.expect(!result.error.empty(), "gmock backend rejects template-specialized target types");
        t.contains(result.error, "does not support template-specialized target types",
                   "gmock backend diagnoses template-specialized target types");
    }

    {
        MockClassInfo cls             = service_mock();
        cls.methods[0].method_name    = "operatorValue";
        cls.methods[0].qualified_name = "fixture::Service::operatorValue";
        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(MockBackend::GMock), {std::move(cls)});
        t.expect(result.error.empty(), "gmock backend accepts normal methods whose names start with operator");
    }

    {
        MockClassInfo cls                     = service_mock();
        cls.methods[0].method_name            = "operator bool";
        cls.methods[0].qualified_name         = "fixture::Service::operator bool";
        cls.methods[0].is_overloaded_operator = false;
        const MockRenderResult result         = gentest::codegen::render::render_mocks(mock_options(MockBackend::GMock), {std::move(cls)});
        t.expect(!result.error.empty(), "gmock backend rejects conversion operators");
        t.contains(result.error, "does not support operator mocks", "gmock backend diagnoses conversion operators");
    }

    {
        MockClassInfo cls             = service_mock();
        cls.methods[0].qualifiers.cv  = MockMethodCvQualifier::Volatile;
        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(MockBackend::GMock), {std::move(cls)});
        t.expect(!result.error.empty(), "gmock backend rejects volatile-qualified methods");
        t.contains(result.error, "does not support volatile-qualified methods", "gmock backend diagnoses volatile-qualified methods");
    }

    {
        CollectorOptions options             = mock_options(MockBackend::GMock);
        options.mock_output_domain_modules   = {"fixture.validation"};
        options.mock_domain_registry_outputs = {"generated/public_mocks_registry.hpp", "generated/public_mocks_registry_module.hpp"};
        options.mock_domain_impl_outputs     = {"generated/public_mocks_inline.hpp", "generated/public_mocks_inline_module.hpp"};
        const MockRenderResult result        = gentest::codegen::render::render_mocks(options, {});
        t.expect(result.error.empty(), "gmock backend accepts unused named-module output domains");
        const MockGeneratedFile *module_registry = find_file(result, "public_mocks_registry_module.hpp");
        t.expect(module_registry != nullptr, "gmock backend emits an empty registry for unused named-module domains");
    }

    {
        MockClassInfo cls             = service_mock();
        cls.methods[0].is_static      = true;
        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(MockBackend::Trompeloeil), {std::move(cls)});
        t.expect(!result.error.empty(), "trompeloeil backend rejects static methods");
        t.contains(result.error, "does not support static methods", "trompeloeil backend diagnoses static methods");
    }

    {
        MockClassInfo cls             = service_mock();
        cls.definition_kind           = MockClassInfo::DefinitionKind::NamedModule;
        cls.definition_module_name    = "fixture.service";
        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(MockBackend::Trompeloeil), {std::move(cls)});
        t.expect(!result.error.empty(), "trompeloeil backend rejects named-module mocks");
        t.contains(result.error, "only supports header/textual mock targets", "trompeloeil backend diagnoses named modules");
    }

    return t.failures == 0 ? 0 : 1;
}
