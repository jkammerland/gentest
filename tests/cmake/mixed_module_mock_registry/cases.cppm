module;

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#endif

export module gentest.mixed_module_cases;

#if !defined(GENTEST_CODEGEN)
import gentest;
import gentest.mock;
#endif

export namespace mixmod {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

} // namespace mixmod

export namespace mixmod {

[[using gentest: test("mixed/module_mock")]]
void module_mock() {
    gentest::mock<Service> service;
    gentest::expect(service, &Service::compute).times(1).with(1).returns(2);

#if !defined(GENTEST_CODEGEN)
    Service &base = service;
    gentest::asserts::EXPECT_EQ(base.compute(1), 2);
#endif
}

} // namespace mixmod
