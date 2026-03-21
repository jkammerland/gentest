module;

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#endif

export module gentest.additive_provider;

#if !defined(GENTEST_CODEGEN)
import gentest;
import gentest.mock;
#endif

export namespace provider {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

} // namespace provider

export namespace provider {

[[using gentest: test("additive/provider_self")]]
void provider_self() {
    gentest::mock<Service> service;
    gentest::expect(service, &Service::compute).times(1).with(3).returns(5);

#if !defined(GENTEST_CODEGEN)
    Service &base = service;
    gentest::asserts::EXPECT_EQ(base.compute(3), 5);
#endif
}

} // namespace provider
