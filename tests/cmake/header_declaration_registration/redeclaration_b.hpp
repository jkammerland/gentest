#pragma once

#include <gentest/attributes.h>

namespace header_declaration_registration {

[[using gentest: test("header_declaration/redeclared")]]
void redeclared_case();

} // namespace header_declaration_registration
