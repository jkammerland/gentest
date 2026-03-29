module;

#include "gentest/mock_impl_codegen.h"

export module gentest.partial_manual_impl;

import gentest;

export [[using gentest: test("partial_manual/impl_only")]]
void impl_only() {}
