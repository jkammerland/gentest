#pragma once

#include <gentest/attributes.h>

namespace header_declaration_registration {

[[using gentest: test("header_declaration/spaced_path")]]
inline void spaced_path_case() {}

} // namespace header_declaration_registration
