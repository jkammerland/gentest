#pragma once

#include <gentest/attributes.h>

namespace header_declaration_registration {

[[using gentest: test("header_declaration/same_basename_a")]]
void same_basename_a();

} // namespace header_declaration_registration
