#pragma once

#include "public/header_registration_mocks.hpp"

#include <gentest/attributes.h>

namespace header_declaration_registration {

[[using gentest: test("header_declaration/explicit_mock")]]
void explicit_mock_case();

} // namespace header_declaration_registration
