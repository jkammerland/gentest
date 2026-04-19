#include "gentest/assertions.h"

using namespace gentest::asserts;

#if defined(GENTEST_TARGET_DEBUG) && defined(GENTEST_TARGET_RELEASE)
#error "target config generator expressions must stay mutually exclusive"
#endif

#if defined(GENTEST_SOURCE_DEBUG) && defined(GENTEST_SOURCE_RELEASE)
#error "source config generator expressions must stay mutually exclusive"
#endif

[[using gentest: test("genex_compile_definitions/builds")]]
void genexCompileDefinitionsBuild() {
    EXPECT_TRUE(true);
}
