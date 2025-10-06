// Shared model types for gentest codegen
//
// These types are passed among discovery, validation, emission and tooling
// components to describe parsed attributes and tests.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::codegen {

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
    std::filesystem::path                mock_registry_path;
    std::filesystem::path                mock_impl_path;
    std::filesystem::path                template_path;
    std::vector<std::string>             sources;
    std::vector<std::string>             clang_args;
    std::optional<std::filesystem::path> compilation_database;
    bool                                 check_only = false;
};

// Description of a discovered test function or member function.
// - qualified_name: fully qualified symbol name used to call the test
// - display_name: display string exposed to users (from test("..."))
// - filename/line: origin information for list/diagnostics
// - tags/requirements/skip/skip_reason: validation results
// - fixture_*: present for member tests; empty for free functions
struct TestCaseInfo {
    std::string              qualified_name;
    std::string              display_name;
    std::string              filename;
    unsigned                 line = 0;
    // Tags and metadata
    std::vector<std::string> tags;
    std::vector<std::string> requirements;
    bool                     should_skip = false;
    std::string              skip_reason;
    // Fixture/method support
    // If non-empty, this case represents a member test on the given fixture type.
    std::string              fixture_qualified_name;
    // True if the enclosing fixture is marked as stateful.
    bool                     fixture_stateful = false;
    // Template instantiation info (for display and call generation)
    std::vector<std::string> template_args;
    // Call-time arguments for free/member tests (e.g., parameterized value list joined by ',').
    std::string              call_arguments;
    // Free-function fixtures declared via [[using gentest: fixtures(A, B, ...)]].
    // These are constructed ephemerally in the wrapper and passed by reference
    // to the test function in declaration order.
    std::vector<std::string> free_fixtures;
};

// Parameter metadata for mocked member functions.
struct MockParamInfo {
    std::string type;     // canonical spelling used in generated signature
    std::string name;     // argument name (auto-assigned when empty)
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
    std::vector<MockMethodInfo> methods;
};

} // namespace gentest::codegen
