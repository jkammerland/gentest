// Template-based emission for test cases.
#pragma once

#include "model.hpp"

#include <optional>
#include <string>
#include <vector>

namespace gentest::codegen {

// Render the generated implementation as a single C++ translation unit.
// Args:
//  - options: entry symbol, optional external template path, sources list
//  - cases: discovered and validated test cases
// Returns: the generated source as a string (or nullopt on error)
auto render_cases(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases) -> std::optional<std::string>;

// Write the rendered content to `options.output_path`. Returns 0 on success.
int emit(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases,
         const std::vector<MockClassInfo> &mocks);

} // namespace gentest::codegen
