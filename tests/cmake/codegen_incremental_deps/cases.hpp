#pragma once

#include "gentest/attributes.h"

namespace depcase {

#if DEP_SWITCH
[[using gentest: test("incremental/compile/on")]]
void compile_variant_on();
#else
[[using gentest: test("incremental/compile/off")]]
void compile_variant_off();
#endif

} // namespace depcase
