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
#include <vector>

namespace gentest::codegen {

enum class FixtureLifetime {
    None,
    MemberEphemeral,
    MemberSuite,
    MemberGlobal,
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
};

// Options consumed by the generator tool entry point.
// - entry: fully qualified function name to emit as the test entry
// - output_path: file to write the generated source into
// - template_path: optional external template path; if empty, built-in used
// - sources: translation units to scan
// - clang_args: extra arguments appended to the underlying clang invocation
// - compilation_database: directory containing compile_commands.json
// - check_only: validate without emitting any output
struct CollectorOptions {
    std::string                          entry = "gentest::run_all_tests";
    std::filesystem::path                output_path;
    std::filesystem::path                tu_output_dir;
    std::filesystem::path                mock_registry_path;
    std::filesystem::path                mock_impl_path;
    std::filesystem::path                template_path;
    std::vector<std::string>             sources;
    std::vector<std::string>             clang_args;
    std::optional<std::filesystem::path> compilation_database;
    std::optional<std::filesystem::path> source_root;
    // Maximum parallelism used when parsing/emitting multiple TUs in TU wrapper mode.
    // 0 selects std::thread::hardware_concurrency().
    std::size_t                          jobs = 0;
    bool                                 include_sources = true;
    bool                                 strict_fixture  = false;
    bool                                 quiet_clang     = false;
    bool                                 check_only = false;
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
    bool        is_benchmark = false;
    bool        is_jitter = false;
    bool        is_baseline = false;
    // True when the test function/method returns a non-void value.
    bool        returns_value = false;
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
    // Free-function fixtures declared via [[using gentest: fixtures(A, B, ...)]].
    // These are constructed ephemerally in the wrapper and passed by reference
    // to the test function in declaration order.
    std::vector<std::string> free_fixtures;
};

// Parameter metadata for mocked member functions.
struct MockParamInfo {
    std::string type;     // canonical spelling used in generated signature
    std::string name;     // argument name (auto-assigned when empty)
    enum class PassStyle {
        Value,
        LValueRef,
        RValueRef,
        ForwardingRef,
    };
    PassStyle pass_style = PassStyle::Value;
    bool      is_const = false;
    bool      is_volatile = false;
};

// Parameter metadata for mocked constructors.
struct MockCtorInfo {
    std::vector<MockParamInfo> parameters;
    std::string                template_prefix; // e.g. "template <typename T, int N>"
    std::vector<std::string>   template_param_names; // e.g. {"T", "N"}
    bool                       is_explicit = false;
    bool                       is_noexcept = false;
};

// Discovered member function suitable for mocking.
struct MockMethodInfo {
    std::string              qualified_name; // e.g. Namespace::Type::method
    std::string              method_name;    // unqualified method identifier
    std::string              return_type;
    std::vector<MockParamInfo> parameters;
    std::string              template_prefix; // e.g. "template <typename T, int N>"
    std::vector<std::string> template_param_names; // e.g. {"T", "N"}
    bool                     is_const = false;
    bool                     is_volatile = false;
    bool                     is_static = false;
    bool                     is_virtual = false;
    bool                     is_pure_virtual = false;
    bool                     is_noexcept = false;
    std::string              ref_qualifier; // "", "&", or "&&"
};

// Mockable class/struct description gathered from AST.
struct MockClassInfo {
    std::string               qualified_name;
    std::string               display_name; // pretty name for diagnostics/codegen
    bool                      derive_for_virtual = false;
    bool                      has_accessible_default_ctor = false;
    bool                      has_virtual_destructor = false;
    std::vector<MockCtorInfo> constructors;
    std::vector<MockMethodInfo> methods;
};

} // namespace gentest::codegen
