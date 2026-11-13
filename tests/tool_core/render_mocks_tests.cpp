#include "mock_manifest.hpp"
#include "render_mocks.hpp"

#include <filesystem>
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
    method.return_type              = "::std::pair<int, int>";
    method.is_virtual               = true;
    method.is_pure_virtual          = true;
    method.qualifiers.cv            = MockMethodCvQualifier::Const;
    method.qualifiers.ref           = MockMethodRefQualifier::LValue;
    method.qualifiers.is_noexcept   = true;
    method.parameters               = {MockParamInfo{.type = "::std::vector<int>", .name = "values"}};
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

MockClassInfo service_mock_without_ref_qualifier() {
    MockClassInfo cls             = service_mock();
    cls.methods[0].qualifiers.ref = MockMethodRefQualifier::None;
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
            t.contains(registry->content, "gentest_mock_state_", "gentest backend keeps native mock state");
            t.excludes(registry->content, "__gentest", "gentest backend emits no reserved double-underscore identifiers");
            t.excludes(registry->content, "#include <gmock/gmock.h>", "gentest backend does not include gmock");
            t.excludes(registry->content, "#include <trompeloeil/mock.hpp>", "gentest backend does not include trompeloeil");
        }
    }

    {
        // Explicit mock targets pass their public include roots; the closest
        // containing root wins, and files outside every root keep the
        // registry-relative spelling.
        CollectorOptions options      = mock_options();
        options.mock_include_roots    = {"vendor", "fixture/..", "fixture"};
        const MockRenderResult result = gentest::codegen::render::render_mocks(options, {service_mock()});
        t.expect(result.error.empty(), "include-root spelling renders without an error");
        const MockGeneratedFile *registry = find_file(result, "public_mocks_registry.hpp");
        t.expect(registry != nullptr, "include-root spelling emits a registry file");
        if (registry != nullptr) {
            t.contains(registry->content, "#include \"service.hpp\"", "closest include root spells the definition header");
            t.excludes(registry->content, "#include \"../fixture/service.hpp\"", "include root replaces registry-relative spelling");
        }
    }

    {
        CollectorOptions options      = mock_options();
        options.mock_include_roots    = {"vendor", "fixture"};
        MockClassInfo outside         = service_mock();
        outside.definition_file       = "other/service.hpp";
        const MockRenderResult result = gentest::codegen::render::render_mocks(options, {outside});
        t.expect(result.error.empty(), "uncovered definition renders without an error");
        const MockGeneratedFile *registry = find_file(result, "public_mocks_registry.hpp");
        t.expect(registry != nullptr, "uncovered definition emits a registry file");
        if (registry != nullptr) {
            t.contains(registry->content, "#include \"../other/service.hpp\"", "uncovered definition keeps registry-relative spelling");
        }
    }

    {
        MockClassInfo cls                       = service_mock();
        cls.methods[0].return_type              = "void";
        cls.methods[0].parameters               = {MockParamInfo{.type = "int &&", .name = "value"}};
        cls.methods[0].parameters[0].pass_style = MockParamInfo::PassStyle::RValueRef;
        const MockRenderResult result           = gentest::codegen::render::render_mocks(mock_options(), {cls});
        t.expect(result.error.empty(), "native backend renders an rvalue-reference parameter");
        const MockGeneratedFile *impl = find_file(result, "public_mocks_inline.hpp");
        t.expect(impl != nullptr, "native backend emits an implementation header");
        if (impl != nullptr) {
            t.contains(impl->content, "dispatch_with_fallback<void, int &&>",
                       "dispatch preserves the declared rvalue reference used by its expectation");
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
            t.contains(registry->content, "using MockReturn0_ = ::std::pair<int, int>;",
                       "gmock backend aliases comma-bearing return types");
            t.contains(registry->content, "using MockArg0_0_ = ::std::vector<int>;", "gmock backend aliases comma-bearing argument types");
            t.contains(registry->content, "MOCK_METHOD(MockReturn0_, compute, (MockArg0_0_), (const, noexcept, ref(&), override));",
                       "gmock backend preserves cv/ref/noexcept/override qualifiers");
            t.excludes(registry->content, "__gentest_mock_", "gmock backend avoids reserved generated aliases");
            t.excludes(registry->content, "#include \"gentest/mock_fwd.h\"", "gmock backend does not include gentest mock forwarding");
            t.excludes(registry->content, "namespace gentest", "gmock backend does not emit into gentest namespace");
            t.excludes(registry->content, "struct mock<", "gmock backend does not specialize gentest::mock");
            t.excludes(registry->content, "detail::MockAccess", "gmock backend does not emit native gentest access plumbing");
        }
        const MockGeneratedFile *impl = find_file(result, "public_mocks_inline.hpp");
        t.expect(impl != nullptr, "gmock backend emits an implementation header");
        if (impl != nullptr) {
            t.contains(impl->content, "gmock mock implementations", "gmock backend names its implementation header");
        }
    }

    {
        const MockRenderResult result =
            gentest::codegen::render::render_mocks(mock_options(MockBackend::Trompeloeil), {service_mock_without_ref_qualifier()});
        t.expect(result.error.empty(), "trompeloeil backend renders without an error");
        const MockGeneratedFile *registry = find_file(result, "public_mocks_registry.hpp");
        t.expect(registry != nullptr, "trompeloeil backend emits a registry file");
        if (registry != nullptr) {
            t.contains(registry->content, "#include <trompeloeil/mock.hpp>", "trompeloeil backend includes trompeloeil");
            t.contains(registry->content, "namespace fixture {\nnamespace mocks {\n", "trompeloeil backend mirrors the target namespace");
            t.contains(registry->content, "struct ServiceMock final : public ::fixture::Service",
                       "trompeloeil backend emits a native mock class");
            t.contains(registry->content, "MAKE_CONST_MOCK1(compute, MockReturn0_(MockArg0_0_), noexcept override);",
                       "trompeloeil backend uses explicit arity and preserves const/noexcept/override qualifiers");
            t.excludes(registry->content, "__gentest_mock_", "trompeloeil backend avoids reserved generated aliases");
            t.excludes(registry->content, "#include \"gentest/mock_fwd.h\"",
                       "trompeloeil backend does not include gentest mock forwarding");
            t.excludes(registry->content, "namespace gentest", "trompeloeil backend does not emit into gentest namespace");
            t.excludes(registry->content, "struct mock<", "trompeloeil backend does not specialize gentest::mock");
            t.excludes(registry->content, "gentest_mock_state_", "trompeloeil backend does not emit native gentest state");
        }
        const MockGeneratedFile *impl = find_file(result, "public_mocks_inline.hpp");
        t.expect(impl != nullptr, "trompeloeil backend emits an implementation header");
        if (impl != nullptr) {
            t.contains(impl->content, "trompeloeil mock implementations", "trompeloeil backend names its implementation header");
        }
    }

    {
        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(MockBackend::Trompeloeil), {service_mock()});
        t.expect(!result.error.empty(), "trompeloeil backend rejects ref-qualified methods");
        t.contains(result.error, "does not support ref-qualified methods", "trompeloeil backend diagnoses ref-qualified methods");
    }

    {
        // Discovery globally qualifies every nested name (#265); the renderer must
        // emit model types verbatim, without re-qualifying template parameters.
        MockClassInfo cls          = service_mock();
        cls.methods[0].return_type = "::std::shared_ptr<::detail::Handle>";
        cls.methods[0].parameters  = {
            MockParamInfo{.type = "::std::function<void (const ::async::Job &, ::detail::Handle *)>", .name = "callback"}};
        cls.methods[0].parameters[0].pass_style = MockParamInfo::PassStyle::Value;
        MockMethodInfo templated;
        templated.qualified_name  = "fixture::Service::collect";
        templated.method_name     = "collect";
        templated.return_type     = "::std::vector<::detail::Handle>";
        templated.template_prefix = "template <typename T>";
        templated.template_params.push_back({.name = "T", .usage_spelling = "T"});
        templated.parameters = {MockParamInfo{.type = "T", .name = "value"},
                                MockParamInfo{.type = "const ::std::vector<T> &", .name = "values"}};
        cls.methods.push_back(templated);

        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(), {cls});
        t.expect(result.error.empty(), "gentest backend renders globally qualified model types");
        const MockGeneratedFile *registry = find_file(result, "public_mocks_registry.hpp");
        t.expect(registry != nullptr, "gentest backend emits a registry for globally qualified model types");
        if (registry != nullptr) {
            t.contains(
                registry->content,
                "::std::shared_ptr<::detail::Handle> compute(::std::function<void (const ::async::Job &, ::detail::Handle *)> callback)",
                "nested names in return and parameter types stay globally qualified");
            t.contains(registry->content, "::std::vector<::detail::Handle> collect(T value, const ::std::vector<T> & values)",
                       "template parameter names stay unqualified");
            t.excludes(registry->content, "::T", "template parameter names are never globally qualified");
        }

        cls.methods.pop_back();
        const MockRenderResult gmock = gentest::codegen::render::render_mocks(mock_options(MockBackend::GMock), {cls});
        t.expect(gmock.error.empty(), "gmock backend renders globally qualified model types");
        const MockGeneratedFile *gmock_registry = find_file(gmock, "public_mocks_registry.hpp");
        if (gmock_registry != nullptr) {
            t.contains(gmock_registry->content, "using MockReturn0_ = ::std::shared_ptr<::detail::Handle>;",
                       "gmock backend aliases globally qualified return types verbatim");
            t.contains(gmock_registry->content, "using MockArg0_0_ = ::std::function<void (const ::async::Job &, ::detail::Handle *)>;",
                       "gmock backend aliases globally qualified argument types verbatim");
        }
    }

    {
        // Discovery places parameter names inside wrapping declarators and spells
        // return types that cannot precede a name through type_identity_t (#276).
        MockClassInfo cls          = service_mock_without_ref_qualifier();
        cls.methods[0].return_type = "::std::type_identity_t<void (*)(int)>";
        cls.methods[0].parameters  = {
            MockParamInfo{.type = "void (*)(int)", .name = "cb", .declaration = "void (*cb)(int)"},
            MockParamInfo{.type = "const int (&)[3]", .name = "values", .declaration = "const int (&values)[3]"},
            MockParamInfo{.type        = "int (::fixture::Target::*)(int) const",
                          .name        = "arg2",
                          .declaration = "int (::fixture::Target::*arg2)(int) const"},
        };
        cls.methods[0].parameters[1].pass_style = MockParamInfo::PassStyle::LValueRef;

        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(), {cls});
        t.expect(result.error.empty(), "gentest backend renders declarator-wrapping types");
        const MockGeneratedFile *registry = find_file(result, "public_mocks_registry.hpp");
        t.expect(registry != nullptr, "gentest backend emits a registry for declarator-wrapping types");
        if (registry != nullptr) {
            t.contains(registry->content,
                       "::std::type_identity_t<void (*)(int)> compute(void (*cb)(int), const int (&values)[3], "
                       "int (::fixture::Target::*arg2)(int) const) const noexcept override;",
                       "parameter names are placed inside their declarators");
            t.contains(registry->content, "#include <type_traits>", "gentest backend makes type_identity_t available");
            t.excludes(registry->content, "void (*)(int) cb", "parameter names never follow a wrapping declarator");
        }
        const MockGeneratedFile *impl = find_file(result, "public_mocks_inline.hpp");
        t.expect(impl != nullptr, "gentest backend emits an implementation for declarator-wrapping types");
        if (impl != nullptr) {
            t.contains(
                impl->content,
                "::std::type_identity_t<void (*)(int)> gentest::mock<::fixture::Service>::compute(void (*cb)(int), const int (&values)[3], "
                "int (::fixture::Target::*arg2)(int) const) const noexcept",
                "out-of-line definitions place parameter names inside their declarators");
            t.contains(impl->content,
                       "static_cast<::std::type_identity_t<void (*)(int)> (::fixture::Service::*)(void (*)(int), const int (&)[3], "
                       "int (::fixture::Target::*)(int) const) const noexcept>",
                       "member-function pointer types compose with a type_identity_t return type");
            t.contains(impl->content,
                       "dispatch_with_fallback<::std::type_identity_t<void (*)(int)>, void (*)(int), const int (&)[3], "
                       "int (::fixture::Target::*)(int) const>",
                       "expectation types compose with a type_identity_t return type");
        }

        cls.methods[0].parameters[2].default_arg = "nullptr";
        for (const auto backend : {MockBackend::GMock, MockBackend::Trompeloeil}) {
            const MockRenderResult   third_party    = gentest::codegen::render::render_mocks(mock_options(backend), {cls});
            const MockGeneratedFile *third_registry = find_file(third_party, "public_mocks_registry.hpp");
            t.expect(third_party.error.empty() && third_registry != nullptr, "third-party backends render declarator-wrapping types");
            if (third_registry != nullptr) {
                t.contains(third_registry->content, "#include <type_traits>", "third-party backends make type_identity_t available");
                t.contains(third_registry->content, "using MockReturn0_ = ::std::type_identity_t<void (*)(int)>;",
                           "third-party backends alias type_identity_t return types");
                t.contains(third_registry->content, "using MockArg0_1_ = const int (&)[3];",
                           "third-party backends alias declarator-wrapping parameter types");
                t.contains(third_registry->content, "MockReturn0_ compute(MockArg0_0_ cb, MockArg0_1_ values) const noexcept",
                           "default-argument overloads name parameters through aliases");
            }
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

    {
        MockClassInfo cls = service_mock();
        cls.methods.clear();

        MockMethodInfo one_arg;
        one_arg.qualified_name  = "fixture::Service::lookup";
        one_arg.method_name     = "lookup";
        one_arg.return_type     = "int";
        one_arg.is_virtual      = true;
        one_arg.is_pure_virtual = true;
        one_arg.parameters      = {MockParamInfo{.type = "int", .name = "value"}};

        MockMethodInfo defaulted = one_arg;
        defaulted.parameters     = {
            MockParamInfo{.type = "int", .name = "value"},
            MockParamInfo{.type = "int", .name = "scale", .default_arg = "2"},
        };
        cls.methods = {one_arg, defaulted};

        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(MockBackend::GMock), {std::move(cls)});
        t.expect(!result.error.empty(), "gmock backend rejects colliding default-argument overload shims");
        t.contains(result.error, "default-argument overload", "gmock backend diagnoses default-argument overload collisions");
    }

    {
        MockClassInfo  cls    = service_mock();
        MockMethodInfo method = compute_method();
        method.qualified_name = "fixture::Service::maybe";
        method.method_name    = "maybe";
        method.return_type    = "int";
        method.parameters     = {MockParamInfo{.type = "int", .name = "value", .default_arg = "fixture::default_value()"}};
        cls.methods           = {method};

        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(MockBackend::GMock), {std::move(cls)});
        t.expect(result.error.empty(), "gmock backend renders noexcept default-argument wrappers");
        const MockGeneratedFile *registry = find_file(result, "public_mocks_registry.hpp");
        t.expect(registry != nullptr, "gmock backend emits a registry for noexcept default-argument wrappers");
        if (registry != nullptr) {
            t.contains(registry->content, "MockReturn0_ maybe() const & noexcept(noexcept(this->maybe(fixture::default_value())))",
                       "gmock default-argument wrapper computes noexcept from the forwarded call");
            t.excludes(registry->content, "MockReturn0_ maybe() const & noexcept {",
                       "gmock default-argument wrapper does not blindly copy noexcept");
        }
    }

    {
        MockClassInfo cls = service_mock();
        cls.unhidden_method_names.emplace_back("stable");

        const MockRenderResult result = gentest::codegen::render::render_mocks(mock_options(MockBackend::GMock), {std::move(cls)});
        t.expect(result.error.empty(), "gmock backend renders base overload using declarations");
        const MockGeneratedFile *registry = find_file(result, "public_mocks_registry.hpp");
        t.expect(registry != nullptr, "gmock backend emits a registry for base overload using declarations");
        if (registry != nullptr) {
            t.contains(registry->content, "using ::fixture::Service::stable;", "gmock backend reintroduces selected base overload sets");
        }
    }

    {
        MockClassInfo cls                        = service_mock();
        cls.enclosing_record_scope               = "fixture::Outer";
        cls.is_template_specialization           = true;
        cls.unhidden_method_names                = {"stable"};
        cls.methods[0].is_final                  = true;
        cls.methods[0].is_variadic               = true;
        cls.methods[0].is_overloaded_operator    = true;
        cls.methods[0].is_conversion_operator    = true;
        cls.methods[0].parameters[0].default_arg = "fixture::kDefault";

        const auto  manifest_path = std::filesystem::temp_directory_path() / "gentest_mock_manifest_roundtrip.json";
        std::string write_error;
        t.expect(gentest::codegen::mock_manifest::write(manifest_path, {cls}, {}, write_error), "mock manifest round trip writes");
        if (!write_error.empty()) {
            std::cerr << "manifest write error: " << write_error << "\n";
        }

        auto read = gentest::codegen::mock_manifest::read(manifest_path);
        std::filesystem::remove(manifest_path);
        t.expect(read.error.empty(), "mock manifest round trip reads");
        if (!read.error.empty()) {
            std::cerr << "manifest read error: " << read.error << "\n";
        }
        t.expect(read.mocks.size() == 1, "mock manifest round trip preserves mock count");
        if (read.mocks.size() == 1) {
            const auto &round_tripped = read.mocks.front();
            t.expect(round_tripped.enclosing_record_scope == "fixture::Outer", "mock manifest preserves nested scope");
            t.expect(round_tripped.is_template_specialization, "mock manifest preserves template-specialization flag");
            t.expect(round_tripped.unhidden_method_names == std::vector<std::string>{"stable"},
                     "mock manifest preserves unhidden base method names");
            t.expect(round_tripped.methods.size() == 1, "mock manifest preserves method count");
            if (round_tripped.methods.size() == 1) {
                const auto &method = round_tripped.methods.front();
                t.expect(method.is_final, "mock manifest preserves final methods");
                t.expect(method.is_variadic, "mock manifest preserves variadic methods");
                t.expect(method.is_overloaded_operator, "mock manifest preserves overloaded operators");
                t.expect(method.is_conversion_operator, "mock manifest preserves conversion operators");
                t.expect(method.parameters[0].default_arg == "fixture::kDefault", "mock manifest preserves parameter default args");
            }
        }
    }

    return t.failures == 0 ? 0 : 1;
}
