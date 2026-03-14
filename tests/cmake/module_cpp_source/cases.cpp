module;

#if defined(GENTEST_CODEGEN)
#include "gentest/runner.h"
#endif

export module gentest.cpp_source_cases;

#if !defined(GENTEST_CODEGEN)
import gentest;
#endif

using namespace gentest::asserts;

export namespace cpp_source {

[[using gentest: test("cpp_source/basic")]]
void basic() {
    EXPECT_EQ(6 * 7, 42);
}

} // namespace cpp_source
