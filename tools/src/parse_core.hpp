// Core attribute list parsing API with no Clang dependencies.
//
// parse_attribute_list: parse a comma-separated list inside
//   [[using gentest: ...]] into ParsedAttribute items.
//
// Returns: a vector of ParsedAttribute with attribute names and argument
// strings (raw + unescaped) in order of appearance. Unknown syntax segments
// are ignored; higher layers validate.
#pragma once

#include "model.hpp"

#include <string_view>
#include <vector>

namespace gentest::codegen {

// Parse comma-separated attribute list into parsed attributes.
auto parse_attribute_list(std::string_view list) -> std::vector<ParsedAttribute>;

} // namespace gentest::codegen
