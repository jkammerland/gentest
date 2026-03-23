import fixture.service_module;

#include "public/fixture_module_mocks.hpp"

int main() {
    fixture::mocks::ServiceModuleMock alias_mock;
    gentest::expect(alias_mock, &fixture::ModuleService::compute).times(1).with(9).returns(18);
    fixture::ModuleService *alias_service = &alias_mock;
    if (alias_service->compute(9) != 18) {
        return 1;
    }

    gentest::mock<fixture::ModuleService> raw_mock;
    gentest::expect(raw_mock, &fixture::ModuleService::compute).times(1).with(10).returns(20);
    fixture::ModuleService *raw_service = &raw_mock;
    if (raw_service->compute(10) != 20) {
        return 2;
    }

    return 0;
}
