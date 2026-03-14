module;

#if defined(GENTEST_CODEGEN)
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE
#endif

export module gentest.multiline_consumer;

import
gentest.multiline_provider;

#if !defined(GENTEST_CODEGEN)
import gentest;
import gentest.mock;
#endif

using namespace gentest::asserts;

export namespace multiline {

[[using gentest: test("multiline/module_mock")]]
void module_mock() {
    gentest::mock<multiline::provider::Service> service;
    gentest::expect(service, &multiline::provider::Service::compute).times(1).with(5).returns(13);

#if !defined(GENTEST_CODEGEN)
    multiline::provider::Service &base = service;
    EXPECT_EQ(base.compute(5), 13);
#endif
}

} // namespace multiline
