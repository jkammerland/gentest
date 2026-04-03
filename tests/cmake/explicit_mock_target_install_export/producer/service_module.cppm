module;
#include <fixture/module_support.hpp>

export module fixture.service_module;

export namespace fixture {

struct ModuleService : detail::ModuleServiceBase {
    virtual ~ModuleService()       = default;
    virtual int compute(int value) = 0;
};

} // namespace fixture
