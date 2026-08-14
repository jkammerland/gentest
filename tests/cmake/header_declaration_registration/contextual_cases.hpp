#pragma once

#include "slot_context.hpp"

#include <gentest/attributes.h>

#ifndef GENTEST_FORCED_CONTEXT
#error "contextual_cases.hpp requires the selected slot's forced include"
#endif

namespace header_declaration_registration {

#if HEADER_CONTEXT_A
[[using gentest: test("header_declaration/context_a")]]
void contextual_case_a();
#else
[[using gentest: test("header_declaration/context_b")]]
void contextual_case_b();
#endif

} // namespace header_declaration_registration
