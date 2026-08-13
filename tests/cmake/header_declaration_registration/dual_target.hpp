#pragma once

#include <gentest/attributes.h>

namespace header_declaration_registration {

#if DUAL_TARGET_VARIANT == 1
[[using gentest: test("header_declaration/dual_one")]]
void dual_target_case();
#else
[[using gentest: test("header_declaration/dual_two")]]
void dual_target_case();
#endif

} // namespace header_declaration_registration
