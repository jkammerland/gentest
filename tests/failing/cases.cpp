#include "gentest/runner.h"
using namespace gentest::asserts;

// Helper type for mocking checks in this suite (global to ease mock codegen)
struct SingleArg {
    void call(int) {}
};

#include "gentest/mock.h"

namespace [[using gentest: suite("failing")]] failing {

[[using gentest: test("single")]]
void will_fail() {
    using namespace gentest::asserts;
    EXPECT_TRUE(false, "non-fatal 1");
    EXPECT_EQ(1, 2, "non-fatal 2");
    ASSERT_TRUE(false, "fatal now");
}

[[using gentest: test("mocking/predicate_mismatch")]]
void predicate_mismatch() {
    using namespace gentest::match;
    gentest::mock<SingleArg> mock_obj;
    gentest::expect(mock_obj, &SingleArg::call).where_args(Eq(3)).times(1);
    // Mismatch: should record a failure due to predicate not matching
    mock_obj.call(4);
}

} // namespace failing
