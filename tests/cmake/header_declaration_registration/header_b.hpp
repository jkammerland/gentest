#pragma once

#include "header_a.hpp"

namespace header_declaration_registration {

[[using gentest: test("header_declaration/b")]]
void case_b(SharedFixture &fixture);

} // namespace header_declaration_registration
