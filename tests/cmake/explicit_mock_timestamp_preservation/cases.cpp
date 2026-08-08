#include "gentest/attributes.h"
#include "gentest/runner.h"
#include "public headers/timestamp mocks.hpp"

namespace timestamp_fixture {

using namespace gentest::asserts;

[[using gentest: test("timestamp_preservation/smoke")]]
void smoke() {
    mocks::InterfaceMock mock;
    gentest::expect(mock, &Interface::value).with(3).returns(7);

    Interface &base = mock;
    EXPECT_EQ(base.value(3), 7);
}

} // namespace timestamp_fixture
