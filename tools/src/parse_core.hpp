// Core attribute list parsing API with no Clang dependencies
#pragma once

#include "model.hpp"

#include <string_view>
#include <vector>

namespace gentest::codegen {

// Parse comma-separated attribute list into parsed attributes.
auto parse_attribute_list(std::string_view list) -> std::vector<ParsedAttribute>;

} // namespace gentest::codegen

