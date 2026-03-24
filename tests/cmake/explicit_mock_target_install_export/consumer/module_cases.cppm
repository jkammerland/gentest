export module fixture.installed_module_codegen_cases;

import gentest;
import fixture.explicit_module_mocks;

using namespace gentest::asserts;

[[using gentest: test("explicit_mock_target/install_export_codegen_module_consumer")]]
void explicit_mock_target_install_export_codegen_module_consumer() {
    fixture::mocks::ServiceModuleMock alias_mock;
    gentest::expect(alias_mock, &fixture::ModuleService::compute).times(1).with(10).returns(30);

    fixture::ModuleService *alias_service = &alias_mock;
    EXPECT_EQ(alias_service->compute(10), 30);

    gentest::mock<fixture::ModuleService> raw_mock;
    gentest::expect(raw_mock, &fixture::ModuleService::compute).times(1).with(11).returns(33);

    fixture::ModuleService *raw_service = &raw_mock;
    EXPECT_EQ(raw_service->compute(11), 33);
}
