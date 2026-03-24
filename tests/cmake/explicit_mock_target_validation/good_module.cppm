export module fixture.validation.good_module;

export import gentest.mock;

export namespace fixture::validation {

struct GoodModuleService {
    virtual ~GoodModuleService() = default;
    virtual int compute(int value) = 0;
};

using GoodModuleServiceMock = gentest::mock <GoodModuleService>;

} // namespace fixture::validation
