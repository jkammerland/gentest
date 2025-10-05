#include "gentest/runner.h"
using namespace gentest::asserts;

namespace [[using gentest: suite("failing")]] failing {

[[using gentest: test("single")]]
void will_fail() {
    using namespace gentest::asserts;
    EXPECT_TRUE(false, "non-fatal 1");
    EXPECT_EQ(1, 2, "non-fatal 2");
    ASSERT_TRUE(false, "fatal now");
}

} // namespace failing
