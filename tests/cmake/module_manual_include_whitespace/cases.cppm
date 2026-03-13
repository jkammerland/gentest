module;

#include "shared_service.hpp"

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#endif

export module gentest.manual_include_whitespace;

#if !defined(GENTEST_CODEGEN)
import gentest;
import gentest.mock;
#endif

using namespace gentest::asserts;

# include "gentest/mock_codegen.h"

export [[using gentest: test("whitespace/manual_spaced_include")]]
void manual_spaced_include() {
    gentest::mock<shared::Service> service;
    gentest::expect(service, &shared::Service::compute).times(1).with(4).returns(9);

#if !defined(GENTEST_CODEGEN)
    shared::Service &base = service;
    EXPECT_EQ(base.compute(4), 9);
#endif
}
