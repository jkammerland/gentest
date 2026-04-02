module;
#include "gentest/attributes.h"

#define HDR       "present_dir_header.hpp"
#define OFF_MACRO 0
#if defined(OFF_MACRO) && OFF_MACRO >= 2
export module wrong_conditional;
#endif
#if !__has_include(HDR)
export module wrong_has_include;
#endif
#if 0b10u == 2u && 010u == 8u && __has_include(HDR)
export module real_conditional;
#endif

export namespace conditional_false_match {
[[using gentest: test("conditional/selected_module")]]
void conditional_selected_module() {}
} // namespace conditional_false_match
