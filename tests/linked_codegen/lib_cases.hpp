#pragma once

#include <gentest/runner.h>

namespace linked_codegen_lib {

struct [[using gentest: fixture(suite)]] LibFixture {
    int value = 7;
};

[[using gentest: test("linked/lib_case")]]
void lib_case(LibFixture &fixture);

} // namespace linked_codegen_lib
