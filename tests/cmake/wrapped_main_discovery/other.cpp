#include <gentest/runner.h>

using namespace gentest::asserts;

[[using gentest: test("wrapped_main/other_case")]]
void other_case() {
    EXPECT_TRUE(true);
}
