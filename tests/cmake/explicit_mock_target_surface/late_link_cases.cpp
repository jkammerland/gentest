#include "gentest/attributes.h"
#include "gentest/runner.h"
#include "public/fixture_header_mocks.hpp"

using namespace gentest::asserts;

[[using gentest: test("explicit_mock_target/late_link_consumer")]]
void late_link_consumer() {
    fixture::mocks::ServiceMock mock_service;
    gentest::expect(mock_service, &fixture::Service::compute).times(1).with(5).returns(13);

    fixture::Service *service = &mock_service;
    EXPECT_EQ(service->compute(5), 13);
}
