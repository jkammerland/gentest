export module fixture.module_mocks;

export import fixture.service_module;

export namespace fixture::mocks {

using ServiceModuleMock = gentest::mock <fixture::ModuleService>;

} // namespace fixture::mocks
