#include "gentest/runner.h"
using namespace gentest::asserts;

#include <stdexcept>

// Helper type for mocking checks in this suite (global to ease mock codegen)
struct SingleArg {
    void call(int) {}
};

#include "gentest/mock.h"

namespace failing {

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

[[using gentest: test("logging/attachment")]]
void logging_attachment() {
    gentest::log_on_fail(true);
    gentest::log("hello from log");
    gentest::log("world from log");
    using namespace gentest::asserts;
    EXPECT_TRUE(false, "trigger failure to capture logs");
}

[[using gentest: test("exceptions/expect_throw_location")]]
void expect_throw_location() {
    EXPECT_THROW((void)0, std::runtime_error);
}

[[using gentest: test("exceptions/expect_no_throw_unknown")]]
void expect_no_throw_unknown() {
    EXPECT_NO_THROW(throw 123);
}

} // namespace failing
