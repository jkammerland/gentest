import fixture.explicit_module_mocks;

int main() {
    fixture::mocks::ServiceModuleMock alias_mock;
    gentest::expect(alias_mock, &fixture::ModuleService::compute).times(1).with(4).returns(12);
    fixture::ModuleService *alias_service = &alias_mock;
    if (alias_service->compute(4) != 12) {
        return 1;
    }

    gentest::mock<fixture::ModuleService> raw_mock;
    gentest::expect(raw_mock, &fixture::ModuleService::compute).times(1).with(6).returns(18);
    fixture::ModuleService *raw_service = &raw_mock;
    if (raw_service->compute(6) != 18) {
        return 2;
    }

    return 0;
}
