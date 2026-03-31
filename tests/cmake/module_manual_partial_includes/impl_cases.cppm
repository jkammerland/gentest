module;

#include "gentest/mock_impl_codegen.h"

export module gentest.manual_partial_impl;

export [[using gentest: test("partial/manual_impl_include")]]
void manual_impl_include() {}
