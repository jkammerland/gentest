module;

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#endif

export module gentest.module_mock_domain_name_is_far_beyond_thirty_two_chars_provider;

#if !defined(GENTEST_CODEGEN)
import gentest;
import gentest.mock;
#endif

export namespace long_domain {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

} // namespace long_domain

export namespace long_domain {

[[using gentest: test("long_domain/provider_self")]]
void provider_self() {
    gentest::mock<Service> service;
    gentest::expect(service, &Service::compute).times(1).with(7).returns(11);

#if !defined(GENTEST_CODEGEN)
    Service &base = service;
    gentest::asserts::EXPECT_EQ(base.compute(7), 11);
#endif
}

} // namespace long_domain
