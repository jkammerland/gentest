#include "gentest/attributes.h"
#include "gentest/runner.h"

namespace ordercase {

using namespace gentest::asserts;

[[using gentest: test("module_first_classic/basic")]]
void basic() {
    EXPECT_EQ(6 * 7, 42);
}

} // namespace ordercase
