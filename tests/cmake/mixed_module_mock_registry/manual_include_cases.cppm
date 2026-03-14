module;

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#endif

export module gentest.mixed_module_manual_include_cases;

#if !defined(GENTEST_CODEGEN)
import gentest;
import gentest.mock;
#endif

export namespace manualinclude {

struct Service {
    virtual ~Service()                = default;
    virtual int compute(int argument) = 0;
};

} // namespace manualinclude

#if !defined(GENTEST_CODEGEN)
#include "gentest/mock_codegen.h"
#endif

export namespace manualinclude {

[[using gentest: test("mixed/manual_include_module_mock")]]
void manual_include_module_mock() {
    gentest::mock<Service> service;
    gentest::expect(service, &Service::compute).times(1).with(9).returns(13);

#if !defined(GENTEST_CODEGEN)
    Service &base = service;
    gentest::asserts::EXPECT_EQ(base.compute(9), 13);
#endif
}

} // namespace manualinclude
