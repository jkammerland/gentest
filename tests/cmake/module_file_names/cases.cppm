export module module_file_names.cases;

import gentest;
import module_file_names.service_mocks;

using namespace gentest::asserts;

export namespace module_file_names::cases {

[[using gentest: test("module_file_names/mock")]]
void module_mock() {
    module_file_names::mocks::ServiceMock mock_service;
    gentest::expect(mock_service, &module_file_names::Service::compute).times(1).with(3).returns(9);
    module_file_names::Service *service = &mock_service;
    EXPECT_EQ(service->compute(3), 9);
}

} // namespace module_file_names::cases
