module;

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#endif

export module gentest.mixed_module_same_block_cases;

#if !defined(GENTEST_CODEGEN)
import gentest;
import gentest.mock;
#endif

using namespace gentest::asserts;

export namespace sameblock {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

[[using gentest: test("mixed/same_block_module_mock")]]
void module_mock_in_same_block() {
    gentest::mock<Service> service;
    gentest::expect(service, &Service::compute).times(1).with(7).returns(11);

#if !defined(GENTEST_CODEGEN)
    Service &base = service;
    EXPECT_EQ(base.compute(7), 11);
#endif
}

} // namespace sameblock
