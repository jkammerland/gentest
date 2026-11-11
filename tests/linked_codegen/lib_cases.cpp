#include "lib_cases.hpp"

namespace linked_codegen_lib {

void lib_case(LibFixture &fixture) { gentest::asserts::EXPECT_EQ(fixture.value, 7); }

} // namespace linked_codegen_lib
