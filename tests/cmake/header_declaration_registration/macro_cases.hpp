#pragma once

#include <gentest/attributes.h>

namespace header_declaration_registration {

#define GENTEST_DECLARE_HEADER_CASE(function_name) [[using gentest: test]] void function_name();

GENTEST_DECLARE_HEADER_CASE(macro_case_one)
GENTEST_DECLARE_HEADER_CASE(macro_case_two)

#define GENTEST_DECLARE_HEADER_PAIR()                                                                                                      \
    [[using gentest: test("header_declaration/macro_pair_one")]] void macro_pair_one();                                                    \
    [[using gentest: test("header_declaration/macro_pair_two")]] void macro_pair_two();

GENTEST_DECLARE_HEADER_PAIR()

#undef GENTEST_DECLARE_HEADER_PAIR
#undef GENTEST_DECLARE_HEADER_CASE

} // namespace header_declaration_registration
