module;

#if defined(GENTEST_CODEGEN)
#include "gentest/runner.h"
#endif

export module gentest.same_line_consumer; import gentest.same_line_provider;

#if !defined(GENTEST_CODEGEN)
import gentest;
#endif

using namespace gentest::asserts;

export namespace same_line {

[[using gentest: test("same_line/basic")]]
void basic() {
    EXPECT_EQ(same_line::provider::value(), 42);
}

} // namespace same_line
