#include "gentest/assertions.h"
#include "gentest/attributes.h"
#include "public/native_ignored_unsupported.hpp"

using namespace gentest::asserts;

[[using gentest: test("mock/native_ignored_unsupported")]]
void native_ignored_unsupported() {
    fixture::validation::mocks::IgnoredUnsupportedServiceMock mock;

    EXPECT_CALL(mock, value).times(1).returns(7);
    fixture::validation::IgnoredUnsupportedService *service = &mock;
    EXPECT_EQ(service->value(), 7);
}
