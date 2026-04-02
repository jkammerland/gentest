export module fixture.service_module;

export namespace fixture {

struct ModuleService {
    virtual ~ModuleService()       = default;
    virtual int compute(int value) = 0;
};

} // namespace fixture
