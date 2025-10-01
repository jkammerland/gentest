// Template-based emission for test cases
#pragma once

#include "model.hpp"

#include <optional>
#include <string>
#include <vector>

namespace gentest::codegen {

auto render_cases(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases) -> std::optional<std::string>;
int  emit(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases);

} // namespace gentest::codegen

