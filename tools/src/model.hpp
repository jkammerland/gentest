// Shared model types for gentest codegen
//
// These types are passed among discovery, validation, emission and tooling
// components to describe parsed attributes and tests.
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gentest::codegen {

enum class FixtureLifetime {
    None,
    MemberEphemeral,
    MemberSuite,
    MemberGlobal,
};

enum class FixtureScope {
    Local,
    Suite,
    Global,
};

enum class ModuleDependencyScanMode {
    Auto,
    Off,
    On,
};

enum class TemplateParamKind {
    Type,
    Value,
    Template,
};

struct TemplateParamInfo {
    TemplateParamKind kind = TemplateParamKind::Type;
    std::string       name;
    bool              is_pack = false;
    // Spelling used when referencing the parameter inside a dependent
    // template-id, for example "T" or "Ts...".
    std::string usage_spelling;
};

struct TemplateBindingSet {
    std::string param_name;
    // Raw candidate bindings as written in the attribute after the parameter
    // name. For packs, each candidate is expected to be a parenthesized row.
    std::vector<std::string> candidates;
};

struct FreeFixtureUse {
    std::string  type_name; // fully qualified (no leading ::)
    FixtureScope scope = FixtureScope::Local;
    std::string  suite_name; // only for suite fixtures
};

enum class FreeCallArgKind {
    Fixture,
    Value,
};

struct FreeCallArg {
    FreeCallArgKind kind = FreeCallArgKind::Fixture;
    // Index into TestCaseInfo::free_fixtures when kind == Fixture.
    std::size_t fixture_index = 0;
    // C++ expression string when kind == Value.
    std::string value_expression;
};

struct FixtureDeclInfo {
    std::string              qualified_name;
    std::string              base_name;
    std::vector<std::string> namespace_parts;
    std::string              suite_name;
    FixtureScope             scope = FixtureScope::Local;
    std::string              tu_filename;
    std::string              filename;
    unsigned                 line = 0;
};

// Parsed attribute name with its argument strings as written in source.
struct ParsedAttribute {
    std::string              name;
    std::vector<std::string> arguments;
};

// Gathered attributes split by namespace context for a declaration.
// `gentest` are the ones we validate strictly; `other_namespaces` are just
// names preserved to report an informational warning.
struct AttributeCollection {
    std::vector<ParsedAttribute> gentest;
    std::vector<std::string>     other_namespaces;
    std::vector<std::string>     mis_scoped_gentest;
};

// Options consumed by the generator tool entry point.
// - entry: fully qualified function name to emit as the test entry
// - sources: translation units to scan
// - clang_args: extra arguments appended to the underlying clang invocation
// - compilation_database: directory containing compile_commands.json
// - check_only: validate without emitting any output
struct CollectorOptions {
    std::string           entry = "gentest::run_all_tests";
    std::filesystem::path tu_output_dir;
    // Optional explicit per-source TU registration headers. When provided in
    // TU mode, this must stay aligned with `sources` and overrides the legacy
    // `<tu_output_dir>/<source>.h` derivation.
    std::vector<std::filesystem::path> tu_output_headers;
    // Build-owned per-source module wrapper outputs. In TU mode this stays
    // aligned with `sources`; any discovered named module source requires a
    // non-empty explicit slot instead of tool-side filename derivation.
    std::vector<std::filesystem::path> module_wrapper_outputs;
    // Build-owned per-source same-module registration implementation outputs.
    // This first-slice mode stays aligned with `sources` and is validated by
    // gentest_codegen after it has classified each module source.
    std::vector<std::filesystem::path> module_registration_outputs;
    // Build-owned per-source textual wrapper outputs. This is used by
    // non-CMake textual integrations that pass owner sources directly to
    // gentest_codegen and need the generator to emit the compilable wrapper TU.
    std::vector<std::filesystem::path> textual_wrapper_outputs;
    std::vector<std::string>           compile_context_ids;
    std::filesystem::path              artifact_manifest_path;
    std::vector<std::filesystem::path> artifact_owner_sources;
    // Build-owned per-domain mock outputs. When mock outputs are requested,
    // these must stay aligned with the ordered mock domain plan: header first,
    // then the first-seen unique named modules in source order.
    std::vector<std::filesystem::path>                                  mock_domain_registry_outputs;
    std::vector<std::filesystem::path>                                  mock_domain_impl_outputs;
    std::vector<std::string>                                            mock_output_domain_modules;
    std::filesystem::path                                               mock_manifest_output_path;
    std::filesystem::path                                               mock_manifest_input_path;
    std::filesystem::path                                               mock_registration_manifest_path;
    std::filesystem::path                                               mock_registry_path;
    std::filesystem::path                                               mock_impl_path;
    std::filesystem::path                                               mock_public_header_path;
    std::filesystem::path                                               mock_aggregate_module_path;
    std::string                                                         mock_aggregate_module_name;
    std::optional<std::filesystem::path>                                depfile_path;
    std::vector<std::string>                                            sources;
    std::unordered_map<std::string, std::string>                        module_interface_names_by_source;
    std::unordered_set<std::string>                                     module_interface_sources;
    std::unordered_map<std::string, std::vector<std::filesystem::path>> explicit_module_sources_by_name;
    std::vector<std::string>                                            clang_args;
    std::optional<std::filesystem::path>                                compilation_database;
    std::optional<std::filesystem::path>                                source_root;
    ModuleDependencyScanMode                                            module_dependency_scan_mode = ModuleDependencyScanMode::Auto;
    std::optional<std::filesystem::path>                                clang_scan_deps_executable;
    // Maximum parallelism used when parsing/emitting multiple TUs in TU wrapper mode.
    // 0 selects std::thread::hardware_concurrency().
    std::size_t jobs           = 0;
    bool        discover_mocks = false;
    bool        strict_fixture = false;
    bool        quiet_clang    = false;
    bool        check_only     = false;
};

// Description of a discovered test function or member function.
// - qualified_name: fully qualified symbol name used to call the test
// - display_name: display string exposed to users (from test("...") and suite prefix)
// - suite_name: logical suite (from enclosing namespace attribute)
// - filename/line: origin information for list/diagnostics
// - tags/requirements/skip/skip_reason: validation results
// - fixture_*: present for member tests; empty for free functions
struct TestCaseInfo {
    std::string qualified_name;
    std::string display_name;
    // Base name (suite/name) used for uniqueness checks across translation units.
    std::string base_name;
    // Translation unit (main file) that produced this case (used for TU-mode grouping).
    std::string tu_filename;
    std::string filename;
    std::string suite_name;
    unsigned    line = 0;
    // Benchmarks: true if discovered via bench("...") attribute
    bool is_benchmark = false;
    bool is_jitter    = false;
    bool is_baseline  = false;
    // True when the discovered callable is declared as a function template.
    bool is_function_template = false;
    // True when the test function/method returns a non-void value.
    bool returns_value = false;
    // True when the discovered callable returns gentest::async_test<T>.
    bool returns_async = false;
    // Tags and metadata
    std::vector<std::string> tags;
    std::vector<std::string> requirements;
    bool                     should_skip = false;
    std::string              skip_reason;
    // Fixture/method support
    // If non-empty, this case represents a member test on the given fixture type.
    std::string     fixture_qualified_name;
    FixtureLifetime fixture_lifetime = FixtureLifetime::None;
    // Template instantiation info (for display and call generation)
    std::vector<std::string> template_args;
    // Call-time arguments for free/member tests (e.g., parameterized value list joined by ',').
    std::string call_arguments;
    // Free-function fixtures inferred from function signature parameter types.
    // Raw fixture type tokens as discovered from the signature; resolved after discovery.
    std::vector<std::string> free_fixture_types;
    // Optional expected scope for each inferred fixture type (same length/order as free_fixture_types).
    // Set when the referenced type declaration is explicitly marked fixture(suite/global).
    std::vector<std::optional<FixtureScope>> free_fixture_required_scopes;
    // Resolved fixture uses with scope/suite metadata.
    std::vector<FreeFixtureUse> free_fixtures;
    // Ordered free-function call argument bindings (supports fixture/value interleaving).
    std::vector<FreeCallArg> free_call_args;
    // Namespace parts for this test (used for fixture resolution).
    std::vector<std::string> namespace_parts;
};

// Parameter metadata for mocked member functions.
struct MockParamInfo {
    std::string type; // canonical spelling used in generated signature
    std::string name; // argument name (auto-assigned when empty)
    enum class PassStyle {
        Value,
        LValueRef,
        RValueRef,
        ForwardingRef,
    };
    PassStyle pass_style = PassStyle::Value;
};

enum class MockMethodCvQualifier {
    None,
    Const,
    Volatile,
    ConstVolatile,
};

enum class MockMethodRefQualifier {
    None,
    LValue,
    RValue,
};

struct MockMethodQualifiers {
    MockMethodCvQualifier  cv          = MockMethodCvQualifier::None;
    MockMethodRefQualifier ref         = MockMethodRefQualifier::None;
    bool                   is_noexcept = false;
};

// Parameter metadata for mocked constructors.
struct MockCtorInfo {
    std::vector<MockParamInfo>     parameters;
    std::string                    template_prefix; // e.g. "template <typename T, int N>"
    std::vector<TemplateParamInfo> template_params;
    bool                           is_explicit = false;
    bool                           is_noexcept = false;
};

// Discovered member function suitable for mocking.
struct MockMethodInfo {
    std::string                    qualified_name; // e.g. Namespace::Type::method
    std::string                    method_name;    // unqualified method identifier
    std::string                    return_type;
    std::vector<MockParamInfo>     parameters;
    std::string                    template_prefix; // e.g. "template <typename T, int N>"
    std::vector<TemplateParamInfo> template_params;
    bool                           is_static       = false;
    bool                           is_virtual      = false;
    bool                           is_pure_virtual = false;
    MockMethodQualifiers           qualifiers;
};

struct MockNamespaceScopeInfo {
    std::string name;
    bool        is_inline           = false;
    bool        is_exported         = false;
    std::size_t lexical_close_group = 0;
    std::string reopen_prefix;
};

// Mockable class/struct description gathered from AST.
struct MockClassInfo {
    enum class DefinitionKind {
        HeaderLike,
        NamedModule,
    };

    std::string qualified_name;
    std::string display_name; // pretty name for diagnostics/codegen
    // Absolute (or normalized) path to the file that contains the target type
    // definition used for registry include generation.
    std::string    definition_file;
    DefinitionKind definition_kind = DefinitionKind::HeaderLike;
    // Source files that instantiate gentest::mock<T> for this target.
    std::vector<std::string> use_files;
    // Owning named module for module-defined mocks. Empty for header-like definitions.
    std::string definition_module_name;
    // Global-scope insertion point within `definition_file` where module-owned
    // mock attachments can be injected safely.
    std::optional<std::size_t> attachment_insertion_offset;
    // Lexical namespace chain enclosing the injected attachment point. When
    // non-empty, emitters close these scopes, inject the specialization at
    // global scope, then reopen them before resuming the source text.
    std::vector<MockNamespaceScopeInfo> attachment_namespace_chain;
    bool                                derive_for_virtual          = false;
    bool                                has_accessible_default_ctor = false;
    bool                                has_virtual_destructor      = false;
    std::vector<MockCtorInfo>           constructors;
    std::vector<MockMethodInfo>         methods;
};

} // namespace gentest::codegen
