#include "gentest/runner.h"
using namespace gentest::asserts;

// Ensure mock target types are visible before the registry is included.
namespace failing {
#include "mocking/types.h"
}

#ifndef GENTEST_BUILDING_MOCKS
#include "gentest/mock.h"
#endif

namespace failing {

#ifndef GENTEST_BUILDING_MOCKS

[[using gentest: test("failing/single")]]
void will_fail() {
    using namespace gentest::asserts;
    EXPECT_TRUE(false, "non-fatal 1");
    EXPECT_EQ(1, 2, "non-fatal 2");
    ASSERT_TRUE(false, "fatal now");
}

[[using gentest: test("failing/mock_args/mismatch")]]
void mock_args_mismatch() {
    gentest::mock<mocking::Calculator> mock_calc;
    gentest::expect(mock_calc, &mocking::Calculator::compute).with(1, 2).returns(3);

    mocking::Calculator *iface = &mock_calc;
    (void)iface->compute(12, 30); // mismatch triggers one failure
}
#endif

} // namespace failing
