#pragma once

#include <gentest/attributes.h>

namespace header_declaration_registration {

[[using gentest: test("header_declaration/same_basename_b")]]
void same_basename_b();

} // namespace header_declaration_registration
