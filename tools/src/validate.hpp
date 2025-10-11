// Validation of gentest attributes into summaries used by generator and tools.
#pragma once

#include "model.hpp"

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace gentest::codegen {

// Summary of function-level attributes after validation.
// - case_name: discovery name (required)
// - tags/requirements: collected metadata
// - should_skip/skip_reason: skip semantics
// - had_error: any validation error encountered (diagnosed via `report`)
struct AttributeSummary {
    std::optional<std::string> case_name;
    std::vector<std::string>   tags;
    std::vector<std::string>   requirements;
    bool                       should_skip = false;
    std::string                skip_reason;
    bool                       had_error = false;
    bool                       is_benchmark = false;
    bool                       is_jitter = false;
    // Template matrix: list of (param, types...)
    std::vector<std::pair<std::string, std::vector<std::string>>> template_sets;
    std::vector<std::pair<std::string, std::vector<std::string>>> template_nttp_sets;
    // Parameterized tests: named parameters with literal values.
    struct ParamSet {
        std::string              param_name; // function parameter name
        std::vector<std::string> values;     // expression tokens
    };
    std::vector<ParamSet> parameter_sets;
    // Parameter generators
    struct RangeSpec { std::string name; std::string start; std::string step; std::string end; };
    struct LinspaceSpec { std::string name; std::string start; std::string end; std::string count; };
    struct GeomSpec { std::string name; std::string start; std::string factor; std::string count; };
    std::vector<RangeSpec>    parameter_ranges;
    std::vector<LinspaceSpec> parameter_linspaces;
    std::vector<GeomSpec>     parameter_geoms;
    // Parameter packs: bundle multiple arguments per test row to avoid Cartesian products.
    struct ParamPack {
        std::vector<std::string>              names; // function parameter names, in order
        std::vector<std::vector<std::string>> rows;
    };
    std::vector<ParamPack> param_packs;
    // Free-function fixtures declared via fixtures(A, B, ...)
    std::vector<std::string> fixtures_types;
};

// Summary of class/struct-level attributes after validation.
// - lifetime: whether the fixture instance is ephemeral, shared per-suite, or global
// - had_error: any validation error encountered (diagnosed via `report`)
struct FixtureAttributeSummary {
    bool            had_error = false;
    FixtureLifetime lifetime  = FixtureLifetime::MemberEphemeral;
};

struct SuiteAttributeSummary {
    bool                       had_error = false;
    std::optional<std::string> suite_name;
};

// Validate a parsed `gentest::` attribute list (function scope) and collect
// metadata.
// Args:
//  - parsed: output of parse_attribute_list
//  - report: callback for each diagnostic message
// Returns: AttributeSummary populated with validated information.
auto validate_attributes(const std::vector<ParsedAttribute> &parsed, const std::function<void(const std::string &)> &report)
    -> AttributeSummary;

// Validate class/struct-level attributes applicable to fixtures.
// Recognized: stateful_fixture (flag). Unknown gentest:: attributes at class
// scope are hard errors; other namespaces are reported by discovery.
// Args/returns: like validate_attributes, but returning fixture semantics only.
auto validate_fixture_attributes(const std::vector<ParsedAttribute> &parsed, const std::function<void(const std::string &)> &report)
    -> FixtureAttributeSummary;

auto validate_namespace_attributes(const std::vector<ParsedAttribute> &parsed, const std::function<void(const std::string &)> &report)
    -> SuiteAttributeSummary;

} // namespace gentest::codegen
