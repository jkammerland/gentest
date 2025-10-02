// Validation of gentest attributes into a summary used by generator and tools
#pragma once

#include "model.hpp"

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace gentest::codegen {

struct AttributeSummary {
    std::optional<std::string> case_name;
    std::vector<std::string>   tags;
    std::vector<std::string>   requirements;
    bool                       should_skip = false;
    std::string                skip_reason;
    bool                       had_error = false;
};

struct FixtureAttributeSummary {
    bool had_error = false;
    bool stateful  = false;
};

// Validate a parsed `gentest::` attribute list and collect metadata.
// `report` is invoked for each diagnostic message.
auto validate_attributes(const std::vector<ParsedAttribute>& parsed,
                         const std::function<void(const std::string&)>& report) -> AttributeSummary;

// Validate class/struct-level attributes applicable to fixtures.
// Recognized:
//  - stateful_fixture (flag)
// Unknown gentest:: attributes at class scope are hard errors.
auto validate_fixture_attributes(const std::vector<ParsedAttribute>& parsed,
                                 const std::function<void(const std::string&)>& report) -> FixtureAttributeSummary;

} // namespace gentest::codegen
