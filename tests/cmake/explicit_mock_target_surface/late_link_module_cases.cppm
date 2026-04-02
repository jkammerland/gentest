module;

export module fixture.late_link_module_consumer;

import gentest;
import fixture.explicit_module_mocks;

using namespace gentest::asserts;

export namespace fixture::late_link_module {

[[using gentest: test("explicit_mock_target/late_link_module_consumer")]]
void late_link_module_consumer() {
    fixture::mocks::ServiceModuleMock mock_service;
    gentest::expect(mock_service, &fixture::ModuleService::compute).times(1).with(5).returns(13);

    fixture::ModuleService *service = &mock_service;
    EXPECT_EQ(service->compute(5), 13);
}

} // namespace fixture::late_link_module
